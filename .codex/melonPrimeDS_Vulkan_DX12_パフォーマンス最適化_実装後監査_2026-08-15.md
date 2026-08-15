# melonPrimeDS Vulkan / DirectX 12 パフォーマンス最適化 実装後監査

- 作成日: 2026-08-15
- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver3`
- 実装前基準HEAD: `9f722a65264807e3daab08e88bc09588404a32dd`
- 監査対象HEAD: `44d6b868a41250658fc8f73744c727b599989b54`
- HEAD commit: `[PERF-006] add presenter descriptor telemetry`
- 比較範囲: `9f722a65264807e3daab08e88bc09588404a32dd..44d6b868a41250658fc8f73744c727b599989b54`
- 比較結果: **11 commits ahead / 0 behind**
- 監査基準: 2026-08-15作成のPERF-001～PERF-007個別実装指示書
- 監査方式: commit単体監査 + 最終HEAD統合監査の2-pass

---

# 1. 総合判定

## **PASS WITH P3 SOURCE / DIAGNOSTIC FINDINGS AND RUNTIME VALIDATION GAPS**

今回の9 commitsについて静的監査した結果、**現在のHEADで描画correctnessを直ちに破壊すると判断できるP1/P2 source findingは確認できなかった**。

特に、元監査でP2としていた以下の主要hot pathは、実装方針として正しく改善されている。

- PERF-004: DX12 `BinResultHeader` copyを廃止し、IndirectArgsへ直接書く
- PERF-003: Native Resolve / GPU readback copyをDisplay Capture需要時だけ実行
- PERF-002: 高解像度compositor outputをsampleable image/textureとしてpresenterへ直接渡す
- PERF-001: CaptureSidecarの依存がない連続lineだけをbatch dispatchする

一方、最終HEADには次のP3 findingが残る。

| ID | Severity | Finding | 判定 |
|---|---:|---|---|
| AUDIT-P3-001 | P3 Diagnostic | PERF-007 `RasterBeginWait*` の用途タグ混入 | **修正済み / source verified** |
| AUDIT-P3-002 | P3 Performance | PERF-006 descriptor churnの実測で残存を確認 | **計測済み / 設計OPEN** |
| AUDIT-P3-003 | P3 Validation | CI・validation layer・外部GPUの最終HEAD証拠が未取得 | **部分検証 / OPEN** |

したがって、**今回のperformance最適化シリーズを「全7件完全CLOSED」とするのはまだ早い**。

ただし、AUDIT-P3-001/002はいずれも現在確認できた範囲では描画semanticsそのものを変更するP1/P2 defectではない。修正は別commitで小さく行うべきであり、PERF-001～005等の描画処理を巻き戻す理由はない。

---

# 2. 今回Pushされたcommit列

実装前HEAD `9f722a65...` 以降は次の順で9 commits追加されている。

| # | Commit | Title | 対象 |
|---:|---|---|---|
| 1 | `e317a1813bbf44691b1430627c625a4c63f74362` | `[PERF-007] measure raster begin fence waits` | PERF-007 |
| 2 | `b80d36498ca558366eefbeb35b8b9fb44a06e23a` | `[PERF-007] preserve disabled telemetry counter parity` | PERF-007 follow-up |
| 3 | `8f45e8f22dcb95b1e1bc0b9997fb8469de9af848` | `[PERF-004] write DX12 indirect args directly` | PERF-004 |
| 4 | `4dd3855466b11dd805bc9e04ab53ecc9b2848983` | `[PERF-006] reuse fixed backend descriptors` | PERF-006 |
| 5 | `db565f9e3837e90fed4387550e15a0a7de4accde` | `[PERF-003] defer native capture readback until demand` | PERF-003 |
| 6 | `a21dea7a560c5e8088a95da1f1802589e7bdfe56` | `[PERF-005] upload structured input deltas` | PERF-005 |
| 7 | `19926cd3c390c06c643c3e15ec51999cfd2963a7` | `[PERF-002] remove high-resolution presenter copies` | PERF-002 |
| 8 | `a8f12f733514abafc067b2dc6f5506c3664130c0` | `[PERF-001] batch independent capture sidecar lines` | PERF-001 |
| 9 | `8c0f1e259b2bfb84a273c5612dd472b6740e85e3` | `[FIX] restore DX12 direct compositor presentation` | PERF-002 follow-up fix |
| 10 | `d8bc31050` | `[PERF-007] isolate raster begin wait telemetry` | PERF-007 telemetry purity |
| 11 | `44d6b868a` | `[PERF-006] add presenter descriptor telemetry` | PERF-006 presenter measurement |

実装内容自体はPERF単位でcommitが分離されており、前回指示した「複数findingを一つの巨大commitへまとめない」という方針は概ね守られている。

---

# 3. Finding summary

## 3.1 PERF別最終状態

| PERF | 元Priority | Backend | 静的監査 | 最終判定 |
|---|---:|---|---|---|
| PERF-007 Raster Begin Wait | P3 Measurement | Vulkan / DX12 | main rendererだけ明示tag、presenter/captureは既定false | **PASS SOURCE** |
| PERF-004 IndirectArgs copy | P2 Medium | DX12 | hot-path copy除去、UAV→INDIRECT→UAV | **PASS STATIC** |
| PERF-006 Descriptor churn | P3 Low～Medium | Vulkan / DX12 | presenter専用counterでDX12/Vulkan direct churnを実測、persistent direct cacheは未設計 | **PARTIAL / MEASURED** |
| PERF-003 Native Resolve/Readback | P2 Medium | Vulkan / DX12 | demand-driven化、same-queue ordering維持 | **PASS STATIC** |
| PERF-005 Structured Input | P3 Medium | Vulkan / DX12 | per-slot generation + dirty range copy | **PASS STATIC** |
| PERF-002 Presenter Copy | P2 High | Vulkan / DX12 | direct image/texture path、fallback保持 | **PASS STATIC after FIX** |
| PERF-001 CaptureSidecar | P2 High | Vulkan / DX12 | conservative classifier + independent run batch | **PASS STATIC** |

`PASS STATIC`は**実機pixel parity / validation layer / debug layerまで合格した意味ではない**。

本監査ではGitHub上の最終sourceとcommit diffを静的に監査した。最終HEADに紐づくCI / workflow runが確認できないため、実機validation criteriaは未確認として分離する。

---

# 4. AUDIT-P3-001 — PERF-007 RasterBeginWait telemetry contamination

- Severity: **P3 Diagnostic**
- 対象: Vulkan / DX12
- 状態: **CLOSED SOURCE / RUNTIME TAG SPLIT**
- 描画correctnessへの直接影響: 現時点でなし
- performance判断への影響: **あり**

## 4.1 問題

PERF-007の指示書では、初回commitのacceptance criteriaとして明示的に次を要求していた。

```text
present/low-latency waitと混同していない
```

しかし現在の実装では計測処理がrenderer固有call siteではなく、汎用command/frame contextへ入っている。

### DX12

`src/DX12Context.cpp`

```cpp
ID3D12GraphicsCommandList* DX12CommandContext::Begin()
{
    ...
    WaitForFence(SubmittedValue, true);
    return ResetList();
}
```

つまり、`DX12CommandContext::Begin()`を呼ぶcontextは、その用途に関係なく`RasterBegin*`として計上される。

実際、presenterも次を使用している。

`src/frontend/qt_sdl/MelonPrimeDX12SurfacePresenter.cpp`

```cpp
OpenList = Commands.Begin();
```

presenter側では別途`PresentBeginWait`も計測しているため、同じwaitが:

```text
PresentBeginWait
+
RasterBeginWait
```

へ二重分類され得る。

### Vulkan

`src/VulkanSync.cpp`のgeneric `FrameRing::BeginFrameInternal(true)`で:

```cpp
VulkanPerf::ScopedRasterBeginWait rasterWait;
fns.WaitForFences(...);
```

としている。

一方、Vulkan presenterも:

```cpp
Vk::FrameContext* frame = Frames.BeginFrame();
```

を使う。

したがってpresenter ringのslot fence waitも`RasterBeginWait*`へ入る。

さらにPERF-003で追加されたcapture用FrameRing等、将来別用途のblocking ringが`BeginFrame()`を使えば同じ問題が拡大する。

## 4.2 なぜ問題か

PERF-007の目的は:

```text
main raster resource reuse waitが本当にbottleneckか
```

を測ってからbehavior changeへ進むか判断することだった。

現在のcounterをそのまま利用すると:

- presenter back-pressure
- present command allocator reuse
- renderer raster resource reuse
- その他generic frame ring reuse

が同じ`RasterBeginWait*`へ混ざる可能性がある。

そのためp99が高くても、renderer raster serializationが原因なのかpresenter側なのか判断できない。

これは描画bugではないが、**PERF-007の計測結果を根拠にframes-in-flightやresource ownershipを変更すると誤診につながる**。

## 4.3 推奨修正

behaviorを一切変えず、計測tagだけcall siteから明示する。

### DX12案

```cpp
ID3D12GraphicsCommandList* DX12CommandContext::Begin(bool recordRasterBegin = false)
{
    ...
    WaitForFence(SubmittedValue, recordRasterBegin);
    return ResetList();
}
```

main rendererだけ:

```cpp
list = Commands.Begin(true);
```

presenter等は:

```cpp
Commands.Begin(); // false
```

とする。

より型安全にするなら:

```cpp
enum class BeginWaitClass
{
    None,
    Raster,
};
```

でもよい。

### Vulkan案

```cpp
FrameContext* FrameRing::BeginFrame(bool recordRasterBegin = false);
```

として、main raster `Frames`だけ`true`を渡す。

presenter / capture等のFrameRingはdefault `false`。

## 4.4 修正後acceptance

```text
renderer raster wait発生
  -> RasterBeginWaitCount +1

presenter waitのみ発生
  -> PresentBeginWait等だけ増加
  -> RasterBeginWaitCountは増えない

capture lazy context waitのみ発生
  -> RasterBeginWaitCountは増えない
```

PERF-007ではこの修正後にbaselineを取り直すこと。

**現状counterで取得したPERF-007 baselineは混入の可能性があるため、最適化判断には使用しない。**

## 4.5 対応結果

`d8bc31050`で用途tagを明示化した。

- DX12: `DX12Renderer3D::RenderFrame()`だけが`Commands.Begin(true)`を使用し、presenterは既定値`false`
- Vulkan: `VulkanRenderer3D::RenderFrame()`だけが`Frames.BeginFrame(true)`を使用し、presenter/captureは既定値`false`
- Vulkanのtimeout/no-wait記録も`recordRasterBegin`に従って抑制する

`build\\release-mingw-x86_64`のWindows構成ビルドはPASSした。DX12実機runtimeでは1x/4x/8x/16xの全実行で`raster_wait_count=0`かつ`raster_no_wait_count`が増加し、presenter側は`present_begin_wait`として別計測された。Vulkan実機runtimeではmain rendererのRasterBegin計測とpresenterの`present_begin_total`/acquire計測が同一ログに出るため、aggregate counterだけでの分離値は断定せず、call-site source splitを根拠とする。

---

# 5. PERF-004 — DX12 IndirectArgs copy除去

- Commit: `8f45e8f22dcb95b1e1bc0b9997fb8469de9af848`
- 判定: **PASS STATIC**

## 5.1 確認できた実装

従来:

```text
CalcOffsets writes BinResult header
→ BinResult UAV -> COPY_SOURCE
→ CopyBufferRegion(BinResult -> IndirectArgs, sizeof(BinResultHeader))
→ BinResult COPY_SOURCE -> UAV
→ IndirectArgs COPY_DEST -> INDIRECT_ARGUMENT
```

現在:

```text
CalcOffsets
  ├─ BinResultの既存headerを維持
  └─ IndirectArgs UAVへ同じindirect argsを直接write

IndirectArgs UAV
→ UAV barrier
→ INDIRECT_ARGUMENT
→ ExecuteIndirect
→ UAV
```

へ変更されている。

`IndirectArgsBuffer`は:

```cpp
D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
D3D12_RESOURCE_STATE_UNORDERED_ACCESS
```

で生成される。

HLSLにも:

```hlsl
RWByteAddressBuffer IndirectArgs : register(u12);
```

が追加され、sort dispatch argumentsとvariant raster argumentsを書いている。

## 5.2 元findingへの効果

元の約49,168 bytes / polygon batchのheader copyと、BinResultのCOPY_SOURCE往復がhot pathから除去されている。

PERF-004のsource-level objectiveは達成している。

## 5.3 残るvalidation

静的にoffset/layoutの意図は一致しているが、次は実機確認が必要。

- multi-batch scene
- variant多数
- translucent polygon
- fog / edge marking
- 1x / 4x / 8x / 16x
- D3D12 debug layer
- `tools/check-dx12-shaders.py`

これらの実行結果は今回GitHub statusから確認できなかった。

---

# 6. AUDIT-P3-002 — PERF-006 descriptor churnは完全には閉じていない

- Severity: **P3 Performance**
- 対象: Vulkan / DX12
- 状態: **PARTIAL / MEASURED / DESIGN OPEN**

## 6.1 良くなった部分

PERF-006 commitではDX12 backendについて:

- frame UAV canonical descriptor table
- compositor slotごとのcanonical descriptor table
- resource recreation時だけUAVをcreate
- frameではshader-visible ringへblock copy

へ変更されている。

これは指示書の安全案と一致している。

Vulkan fallback presenterについても:

- persistent descriptor pool
- layer × samplerのpersistent descriptor set
- image recreate時だけupdate

になっている。

またVulkan `EnsureLayerImage()`は既存imageを再生成する際に:

```cpp
Frames.WaitIdle();
texture.Image.Destroy();
...
UpdateLayerDescriptorSets(layer);
```

としており、現在確認できる範囲ではin-flight descriptorを無断updateする構造にはなっていない。

この点はcorrectness上良い。

## 6.2 DX12 presenterに元churnが残っている

現在の:

`src/frontend/qt_sdl/MelonPrimeDX12SurfacePresenter.cpp`

ではdrawごとに:

```cpp
Descriptors.Allocate(1, cpu, gpu);
Context->GetDevice()->CreateShaderResourceView(source, &desc, cpu);
```

を実行している。

つまり指示書の:

```text
DX12 presenter:
layer texture/SRVがresource recreationまで固定ならCPU descriptorをpersistent化
```

は実装されていない。

さらにこの`CreateShaderResourceView()`は現在の`DescriptorCreateCount`等へ明示的に加算されていないため、**PERF-006 telemetryだけを見るとpresenter churnを過小評価する**。

## 6.3 VulkanはPERF-002 direct image導入後にsteady-state allocate/updateが再発

PERF-006時点ではfallback layer descriptorをpersistent化したが、後続PERF-002でnormal structured presentationがdirect imageへ移った。

現在の:

```cpp
VulkanPresenter::UploadLayerFromImage(...)
```

では、direct imageを使うたびにper-frame descriptor poolから:

```cpp
AllocateDescriptorSets(... 2 sets ...)
UpdateDescriptorSets(... 2 writes ...)
```

を行う。

上画面・下画面の両方なら通常frameで最大4 sets / 4 writes相当になる。

これはresource lifetime上安全なtransient pathだが、PERF-006のperformance goal:

```text
Vulkan presenterではsteady-state drawごとのallocate/updateがなくなる
```

は最終HEADでは成立しない。

## 6.4 判定

PERF-006は:

```text
backend fixed UAV churn: 改善済み
Vulkan fallback layer descriptor: 改善済み
DX12 presenter SRV create: 残存
Vulkan direct-image descriptor allocate/update: 後続実装で再残存
```

である。

したがって**PERF-006をCLOSED扱いせずPARTIALとする**。

## 6.5 推奨方針

これはP3なので、PERF-002 direct pathを崩してまで急いで直さない。

まずcounterを完全にする。

### DX12 presenter

最低限:

```text
PresenterSrvCreateCount
PresenterDescriptorCopyCount
PresenterDescriptorCpuTimeNs
```

を追加し、`CreateShaderResourceView()`を計測対象に含める。

fallback layerはresource recreationまで固定なのでCPU-only canonical SRVを作り、frame heapへcopyする方式が安全。

### Vulkan direct image

direct imageはcompositorの3 slots × top/bottomでresource/view自体はslot lifetime中固定である。

ただしrendererが所有するviewをpresenterがpersistent descriptorとして保持するため、resource recreation / output generation / lease lifetimeを明確化せず単純cacheしてはいけない。

安全に行うなら別PERFとして:

```text
renderer output resource generation
+ direct image view identity
+ persistent presenter descriptor cache
+ generation change時のquiesce/invalidate
```

を設計してから実装する。

**今回の修正commitへ混ぜないこと。**

## 6.6 対応結果と実測

`44d6b868a`で専用counterを追加した。

```text
DX12: presenter_srv_creates
      presenter_descriptor_copies
      presenter_descriptor_cpu_ns
Vulkan: presenter_srv_creates (direct descriptor-set creation相当)
        presenter_descriptor_copies
        presenter_descriptor_cpu_ns
```

RTX 5070 Tiの最新HEAD実機runtimeで、Metroid Prime Huntersのsavestate slot 7を同一条件で1x/4x/8x/16x実行した。

| Backend | scale | frames/sample | presenter create実測 | copy | CPU time実測 |
|---|---:|---:|---:|---:|---:|
| DX12 | 1 | 約60 | 180前後 | 0 | 約0.15～0.24 ms/report |
| DX12 | 4 | 約60 | 120～122 | 0 | 約0.16～0.18 ms/report |
| DX12 | 8 | 約60 | 180～183 | 0 | 約0.15～0.18 ms/report |
| DX12 | 16 | 約60 | 148～183 | 0 | 約0.14～0.18 ms/report |
| Vulkan | 1/4/8/16 | 約60 | 240 | 0 | 約0.09～0.12 ms/report |

全実行で`PresentedScreenCopyBytes=0`、`DirectCompositorImageFrames`増加、画面キャプチャ正常を確認した。したがってtelemetry要件は完了したが、Vulkan direct imageのpersistent descriptor cacheはresource generation/view identity/lease quiesceを伴う別設計が必要であり、本監査では**PARTIALのまま閉じない**。

---

# 7. PERF-003 — Native Resolve / Readback Capture-Demand化

- Commit: `db565f9e3837e90fed4387550e15a0a7de4accde`
- 判定: **PASS STATIC**

## 7.1 DX12

通常RenderFrame末尾から:

```text
Resolve dispatch
ResolveBuffer -> ReadbackBuffer copy
```

が削除されている。

代わりに`EnsureFrameReadback()`の最初の需要時に:

```text
RecordNativeResolveAndReadback()
→ capture専用command contextへrecord
→ same direct queueへsubmit
→ capture contextだけwait
→ map/copy
```

となっている。

`CaptureCommands.WaitIdle()`はcapture専用contextのallocator再利用・readback完了待ちであり、通常frameへqueue-wide idleを追加する実装ではない。

## 7.2 Vulkan

同様に`CaptureFrames` 1-slot ringを追加し、native readback需要時だけResolve + copyをsame queueへsubmitする。

main renderer / compositorと同じqueue family・queue orderingを利用し、steady-state device-wide idleは追加していない。

## 7.3 mid-frame capture change

現在の`GPU_Soft.cpp`ではframe開始時だけでなく、各capture lineでも:

```cpp
if (captureNeeds3D)
{
    if (!StructuredCapturePreparedThisFrame)
    {
        Rend3D->BeginCaptureFrame();
        Rend3D->PrepareCaptureFrame();
        StructuredCapturePreparedThisFrame = true;
    }
    Output3D = Rend3D->GetLine(...);
}
```

を行う。

したがって、単純なframe-start `GPU.CaptureEnable` gatingだけに依存する設計にはなっていない。

これは元指示書の重要条件を満たす方向である。

## 7.4 残るvalidation

実機で最低限:

```text
Captureなし
Captureあり・3D source不要
Captureあり・3D source必要
mid-frame register/source change
identical 3D frame + capture demand
```

を確認すること。

期待counter:

```text
3D capture不要:
  NativeResolveCount == 0
  NativeReadbackCopyBytes == 0

3D capture需要あり:
  NativeResolveCount == 1/frame
  NativeReadbackCopyBytes == 196608/frame
```

---

# 8. PERF-005 — Structured Input delta upload

- Commit: `a21dea7a560c5e8088a95da1f1802589e7bdfe56`
- 判定: **PASS STATIC**

## 8.1 実装確認

14 planes + 2 line metadata + capture commandsについて:

```cpp
StructuredComposition::GenerationState
```

を追加し、producer側で値が実際に変化したunitだけgenerationを更新する。

compositorの3 output slotsそれぞれが:

```text
UploadedContentGeneration
StructuredUploadInitialized
```

を持つ。

これは重要で、単純な「前frameとの差分」ではなく**各slotに最後に何をuploadしたか**を比較している。

そのため3-slot reuseで:

```text
slot0 -> slot1 -> slot2 -> slot0
```

となってもslot0が古いdataを保持したままzero-copy判定される設計にはなっていない。

## 8.2 copy region

変更unitをlogical rangesへ変換し、隣接rangeをmergeした上で:

- DX12: 必要rangeだけ`CopyBufferRegion`
- Vulkan: 必要rangeだけ`VkBufferCopy`をまとめて`CmdCopyBuffer`

している。

buffer layout / shader-side structured layoutは変更していない。

## 8.3 PERF-001との統合

PERF-001 classifierはcapture commandだけでなくreference planes 3 / 7 / 13にも依存する。

そのためPERF-001追加後はclassification用staged commandを再生成するdirty条件に:

```text
CaptureCommands
Plane[3]
Plane[7]
Plane[13]
```

を含めている。

これはPERF-005 partial uploadとPERF-001 classifierの統合上必要な処理であり、見落とされていない。

## 8.4 残るvalidation

- static sceneでupload bytesが2,757,120 bytes/frame未満
- 3-slot reuse長時間
- scale 1x→16x→1x
- renderer switch
- savestate load
- capture metadataのみ変化

は実機確認が必要。

---

# 9. PERF-002 — 高解像度Presenter Copy除去

- Main commit: `19926cd3c390c06c643c3e15ec51999cfd2963a7`
- Follow-up fix: `8c0f1e259b2bfb84a273c5612dd472b6740e85e3`
- 判定: **PASS STATIC after FIX**

## 9.1 Vulkan

compositor output slotに:

```text
DirectImageTop
DirectImageBottom
```

を追加。

format `VK_FORMAT_R8G8B8A8_UNORM`について:

```text
VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT
VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
```

をprobeし、unsupportedなら従来buffer pathへfallbackする。

normal direct pathでは:

```text
compositor compute -> direct VkImage GENERAL
→ SHADER_READ_ONLY_OPTIMAL
→ presenterが同image viewをsample
```

となり、従来の:

```text
composed buffer
→ vkCmdCopyBufferToImage(top)
→ vkCmdCopyBufferToImage(bottom)
```

を回避する。

## 9.2 DX12

compositor slotに2-slice:

```text
DXGI_FORMAT_R8G8B8A8_UNORM Texture2DArray
```

を作り:

```text
UNORDERED_ACCESS
→ compositor write
→ PIXEL_SHADER_RESOURCE
→ presenter SRV sample
```

とする。

format supportとしてtexture2D / shader sample / typed UAV / typed storeを確認し、unsupportedなら従来buffer pathへfallbackする。

## 9.3 follow-up fixの意味

PERF-002初回commit後、DX12 direct outputは`Texture2DArray` SRVである一方、present shader sourceが`Texture2D<float4>`であった。

最新commit `8c0f1e2...`で:

```hlsl
Texture2DArray<float4> Source : register(t0);
```

へ修正し、sampleも:

```hlsl
Source.Sample(..., float3(uv, 0.0))
```

へ変更している。

fallbackの通常1-slice Texture2DもSRV側で`TEXTURE2DARRAY`の1 slice viewとして統一した。

present shaderはruntime `CompileShader()`されるため、現在のsource変更がpipeline作成へ反映される構造である。

このfollow-upにより、今回series中に発生していたDX12 direct presentation resource type mismatchは**最終HEADではCLOSED**と判断する。

## 9.4 残るvalidation

この最適化は96 MiB/frame級の16x screen copyを消す重要変更なので、静的PASSだけで終了しないこと。

必須:

```text
Vulkan / DX12
1x / 4x / 8x / 16x
windowed / fullscreen
screen swap
bitmap text
texture edge
fog
edge marking
radar
Custom HUD
OSD
Display Capture
```

normal direct pathで:

```text
PresentedScreenCopyBytes == 0
DirectCompositorImageFrames > 0
FallbackCompositorBufferFrames == 0
```

を確認する。

unsupported pathを試せる場合はfallbackも確認する。

---

# 10. PERF-001 — CaptureSidecar independent-run batching

- Commit: `a8f12f733514abafc067b2dc6f5506c3664130c0`
- 判定: **PASS STATIC**

## 10.1 classifier方針

今回の実装は「全lineを無条件batch化」していない。

`AnalyzeCaptureDependencies()`でvalid capture lineを解析し、次のlineをlegacy orderedへ落とす。

- sidecar referenceを読む
- destination write spanが別valid lineと同bank/versionでoverlap
- malformed command
- dependencyを安全に証明できない

これは元指示書の:

```text
false negativeは性能を失うだけ
false positiveは描画順序を壊す
```

というcorrectness優先方針に合っている。

## 10.2 wraparound

16-bit capture addressについて:

```text
address + width > 0x10000
```

ならwrite spanを2区間へ分け、wrap先とのoverlapも検出している。

producer側もdestination/source addressを`0xFFFF` maskで扱っており、単位は整合している。

## 10.3 batching

consecutive independent linesだけをrunにし:

- DX12: `Dispatch(..., z = runLength)`
- Vulkan: `CmdDispatch(..., z = runLength)`

する。

run終了後にはCaptureSidecar barrierを1回残す。

legacy lineは従来通り:

```text
1 line dispatch
→ barrier
```

を維持する。

つまりlegacy lineを跨いで前後のindependent lineをまとめる実装ではなく、line orderを保ったまま連続区間だけをbatchする。

## 10.4 PERF-002とのmode bit共存

DX12では既存`DispatchPad` lower bitをdirect compositor modeに使い、batch modeをbit 1へ追加している。

```text
bit 0: direct output
bit 1: capture batch
```

shader側も:

```hlsl
(DispatchPad & 1u)
(DispatchPad & 2u)
```

で分離しており、PERF-002のdirect output flagを上書きしていない。

Vulkanもshared push constant内でcapture batch modeを分離している。

## 10.5 classifier自体のCPU cost

correctness上の問題ではないが、classifierは各valid lineについてsource referenceを最大256 pixels走査し、write spansについてvalid line同士をpairwise比較する。

上限規模は小さいものの、CaptureSidecar dispatch削減効果とCPU classifier costの差し引きは実測すること。

必須counter:

```text
CaptureValidLineCount
CaptureIndependentLineCount
CaptureLegacyOrderedLineCount
CaptureSidecarDispatchCount
CaptureSidecarBarrierCount
```

期待:

```text
IndependentLineCount > 1 のframeで
CaptureSidecarDispatchCount < CaptureValidLineCount
```

---

# 11. PERF-007以外の同期・resource lifetime統合監査

## 11.1 Vulkan direct image

compositor slotのdirect imageはcompositor submit時に:

```text
previous layout
→ GENERAL / compute write
→ SHADER_READ_ONLY_OPTIMAL / fragment read
```

へ遷移する。

presenterはleased output slotのimage viewをsampleする。

次回同slotをcompositorが再利用するときは、renderer側`Vk::Image`が保持するlayout stateから再びGENERALへ遷移する。

同じmain queue上のsubmission orderとoutput leaseでslot reuseを制御しているため、静的にはproducer/presenterの順序モデルは成立している。

## 11.2 DX12 direct texture

renderer側slotは:

```text
UAV
→ PIXEL_SHADER_RESOURCE
```

へ遷移してpublishする。

presenterはdirect resourceをPS_RESOURCEとして受け取り、direct pathではEndFrame時にUAVへ戻さない。

次にrendererが同slotを再利用するときに:

```text
PIXEL_SHADER_RESOURCE
→ UNORDERED_ACCESS
```

へ戻す。

こちらもsame direct queue orderingを前提とする設計になっている。

## 11.3 Vulkan persistent fallback descriptor safety

fallback layer imageのsize/reallocation時には`Frames.WaitIdle()`してからimageをdestroyし、persistent descriptorをupdateする。

よって現sourceではPERF-006で懸念した:

```text
GPUがdescriptorを使用中なのに同setを書き換える
```

という明確なunsafe patternは確認できなかった。

---

# 12. CI / test evidence

GitHub connectorから最終HEAD `8c0f1e2...`について確認した範囲では:

```text
combined status: status entriesなし
workflow runs: なし
```

であった。

またbranch protection上required status checksも設定されていない。

したがって、本監査では次を**実行済みと断定しない**。

- `git diff --check`
- `python tools/check-dx12-shaders.py`
- Vulkan shader regeneration/check
- Windows DX12 build
- Windows/Linux Vulkan build
- Vulkan validation layer
- D3D12 debug layer
- 1x/4x/8x/16x pixel parity
- Display Capture parity
- fullscreen / resize / renderer switch
- NVIDIA Reflex / AMD Anti-Lag 2 / Intel XeLL併用

source上にテストfixtureやgenerated shader更新が含まれることと、CI/runtimeでPASSしたことは別である。

## 12.1 対応後のlocal evidence

この追補では、上記のCI未取得とは分離して、現HEAD `44d6b868a`をcheckoutしたワークツリーで次を実行した。

- `cmd /c tools\\build\\windows\\build-mingw-existing.bat --jobs 1 --tail 100`: PASS
- build内のstructured capture dependency vectors: PASS（1x/4x/8x/16x、Vulkan/DX12 shared classifier）
- Vulkan present timing model tests: PASS
- Vulkan present pacer fake-dispatch tests: PASS
- Vulkan renderer fallback tests: PASS
- Intel XeLL fake API state-machine tests: PASS
- `git diff --check`: PASS
- DX12/Vulkan runtime capture: 1x/4x/8x/16x、各6枚の正常画面を確認
- `python tools/ci/audits/check-dx12-shaders.py`: PASS（111 variants、scale 1/5/9）
- `run-raster-differential.ps1 -Renderer DX12`: 1x 5 frames exact、mismatchedPixels=0
- `run-raster-differential.ps1 -Renderer Vulkan`: 1x 5 frames exact、mismatchedPixels=0

未取得のまま残る証拠は、最終HEADに紐づくGitHub Actions run、Vulkan validation layer/D3D12 debug-layerのログ保存、AMD/Intel実機、Linux/macOS、Display Capture専用parity、low-latency全組合せである。これらはRTX 5070 TiのWindows実行結果で代替しない。

---

# 13. 実機validation checklist

## 13.1 共通

- [ ] 1xでSoftware Rendering / OpenGL Compute referenceと主要sceneを比較
- [ ] 4x / 8x / 16xで同一sceneを確認
- [ ] top/bottom routing
- [ ] screen swap
- [ ] window resize
- [ ] fullscreen toggle
- [ ] renderer switch
- [ ] game reset
- [ ] savestate load
- [ ] 長時間3-slot reuse

## 13.2 PERF-004 DX12

- [ ] `DX12IndirectArgsCopyBytes == 0`
- [ ] `DX12IndirectArgsCopyCount == 0`
- [ ] polygon multi-batchで欠落なし
- [ ] D3D12 debug layer error 0

## 13.3 PERF-003

Capture不要:

- [ ] `NativeResolveCount == 0`
- [ ] `NativeReadbackCopyBytes == 0`

Capture必要:

- [ ] `NativeResolveCount == 1/frame`
- [ ] `NativeReadbackCopyBytes == 196608/frame`
- [ ] first demand後の同frame追加GetLineでresubmitなし
- [ ] mid-frame capture変更一致

## 13.4 PERF-005

- [ ] slot初回 full upload
- [ ] static sceneでuploaded bytesが2,757,120 B/frame未満
- [ ] 3-slot reuseで3frame周期ちらつきなし
- [ ] routingだけ変更して反映遅延なし
- [ ] capture metadataだけ変更して反映遅延なし

## 13.5 PERF-002

- [ ] Vulkan normal direct path `PresentedScreenCopyBytes == 0`
- [ ] DX12 normal direct path `PresentedScreenCopyBytes == 0`
- [ ] `DirectCompositorImageFrames`増加
- [ ] text blur/offsetなし
- [ ] texture edge変化なし
- [ ] radar filtering変化なし
- [ ] RGBA/BGRA channel差なし
- [ ] unsupported device fallback確認可能なら実施

## 13.6 PERF-001

- [ ] independent runでdispatch count削減
- [ ] legacy dependency patternはbatchへ入らない
- [ ] Capture残像なし
- [ ] 1frameおきの順序変化なし
- [ ] Vulkan synchronization warningなし
- [ ] D3D12 resource hazardなし

## 13.7 low-latency機能

性能最適化によりlow-latency pacingを壊していないことを確認する。

- [ ] Reflex OFF
- [ ] Reflex ON
- [ ] Reflex BOOST
- [ ] AMD Anti-Lag 2 OFF / ON
- [ ] Intel XeLL supported環境があればOFF / ON
- [ ] VSync OFF / ON

PERF-007 counter修正前の`RasterBeginWait*`値はこの判断に使用しない。

## 13.8 対応後runtime結果

RTX 5070 Ti / Windowsのlocal runtimeで、DX12とVulkanを1x/4x/8x/16xで起動し、savestate load後のMetroid Prime Hunters画面を各6枚保存した。DX12/Vulkanとも黒画面・崩れ・direct compositorの画面欠落は再現せず、top/bottom画面とradarを含む通常画面を確認した。別系統のraster differentialでもDX12/Vulkan 1xを各5フレームSoftware referenceと比較し、全フレーム`mismatchedPixels=0`だった。

対応後ログで確認できた範囲:

- DX12 `DX12IndirectArgsCopyBytes=0`, `DX12IndirectArgsCopyCount=0`
- DX12/Vulkan `PresentedScreenCopyBytes=0`
- DX12/Vulkan `DirectCompositorImageFrames`増加
- DX12/Vulkan descriptor専用counterがscale別に出力される
- DX12通常実行の`RasterBeginWaitCount=0`、Vulkanのmain renderer RasterBegin計測はpresenter計測とcall-siteで分離

resize/fullscreen/renderer switch、Display Capture、validation/debug-layer、AMD/Intel/Linux/macOS、Reflex/Anti-Lag/XeLL全組合せはこの環境では未取得であり、OPENとして残す。

---

# 14. 修正優先順位

## 14.1 次に直すもの — AUDIT-P3-001のみ

最初にPERF-007 telemetry tagを修正する。

これは:

- shader変更なし
- resource layout変更なし
- dispatch変更なし
- frames-in-flight変更なし
- renderer output変更なし

で実施できる。

**このcommitには他のperformance変更を混ぜない。**

修正後にPERF-007 baselineを取り直す。

## 14.2 AUDIT-P3-002は計測してから

PERF-006 descriptor残件はP3であり、PERF-002 direct image pathを複雑化してまで即修正する必要はない。

まずDX12 presenterを含むdescriptor telemetryを完全にし、CPU costが有意なら別MD / 別commitで行う。

## 14.3 PERF-001～005を同時に再編集しない

現在静的に成立している:

- capture dependency classifier
- direct image ownership
- demand readback state machine
- structured generation tracking
- indirect args direct write

は互いに依存している。

P3 telemetry修正のついでにこれらをcleanup/rewriteしない。

---

# 15. 最終結論

今回のPushは、前回のperformance bottleneck監査で挙げた主要P2 hot pathに対して、**概ね狙いどおりのsource-level最適化を実装している**。

特に:

```text
PERF-004 DX12 IndirectArgs copy
PERF-003 unconditional native readback
PERF-002 O(scale^2) presenter screen copy
PERF-001 line-by-line CaptureSidecar dispatch
```

は、現在の最終HEADではそれぞれ直接的な改善コードが確認できた。

PERF-002で一度発生していたDX12 direct texture/present shader resource type mismatchも、最新HEAD `8c0f1e2...`で`Texture2DArray`へ統一されており、静的には修正済みである。

一方で、完全CLOSED前に残るものは次の3点。

```text
1. PERF-007 RasterBegin telemetryの用途混入を修正する。
2. PERF-006はPARTIALとして残し、DX12 presenter / Vulkan direct pathのdescriptor churnを実測する。
3. CI/runtime evidenceがないため、1x/4x/8x/16x + validation/debug layer + Display Capture parityを実機で完了する。
```

### 最終Severity

```text
P1 source findings: 0
P2 source findings: 0
P3 source/diagnostic findings: 2
P3 validation gap groups: 1
```

### 最終判定

**PASS WITH P3 SOURCE / DIAGNOSTIC FINDINGS AND RUNTIME VALIDATION GAPS**

上記の「次のcode change」記述は修正前時点の履歴であり、現HEADでは`d8bc31050`と`44d6b868a`で対応済みである。

---

# 16. 対応後再監査（現HEAD `44d6b868a`）

## 16.1 完了した対応

- PERF-007: RasterBegin telemetryの用途tagをmain rendererへ限定し、presenter/capture/compositorの待機を混入させない形へ修正。単独commit `d8bc31050`。
- PERF-006: DX12 presenterのSRV作成、Vulkan direct presenterのdescriptor作成相当を専用counter・CPU時間で計測。別commit `44d6b868a`。
- local Windows build、既存fake tests、DX12 shader audit、DX12/Vulkan 1x raster differentialを現HEADで再実行。
- RTX 5070 Ti runtimeでDX12/Vulkan 1x/4x/8x/16xを各6枚captureし、direct pathの正常画面と`PresentedScreenCopyBytes=0`を確認。

## 16.2 残るOPEN

- Vulkan direct descriptorのpersistent cacheは、renderer output resource generation、view identity、lease quiesce/invalidateを別設計してから実装する。現状は安全なtransient allocate/updateを維持する。
- GitHub Actionsのexact-head run、Vulkan validation layer、D3D12 debug-layer保存ログ、Display Capture専用parity、AMD/Intel/Linux/macOS、low-latency全組合せは未取得。

### 現時点の判定

**PASS WITH P3 PERFORMANCE DESIGN OPEN AND EXTERNAL VALIDATION GAPS**

描画correctnessに関するlocal evidenceはPASSしているが、上記の外部証拠とVulkan direct descriptor cache設計が未完了のため、全7 PERFを完全CLOSEDとは記録しない。
