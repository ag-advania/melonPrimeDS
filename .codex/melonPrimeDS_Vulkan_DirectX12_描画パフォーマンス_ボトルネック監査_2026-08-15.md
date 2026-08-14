# melonPrimeDS Vulkan / DirectX 12 描画パフォーマンス・ボトルネック監査

- 作成日: 2026-08-15
- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver3`
- 監査HEAD: `6cd32fc34d4a41d7797c5823ee62ca11a9efc467`
- Tree: `02382dcbb6cda646ae4896460a5cc89420ed75ae`
- HEAD commit: `Load versioned BSD Vulkan runtimes`
- 監査種別: 静的コード監査（Vulkan / DirectX 12 の描画・compositor・present hot path）
- 総合判定: **PASS WITH P2 PERFORMANCE OPTIMIZATION FINDINGS**
- P1: **0件**
- P2: **4件**
- P3: **3件**

---

## 1. 結論

Vulkan / DirectX 12 の現行実装には、**毎フレーム `vkDeviceWaitIdle()` / queue-wide idle を実行するような致命的なP1ボトルネックは確認できなかった**。

また、steady-stateの描画経路では以下が既に適切に実装されている。

- GPUリソースの大半を初期化時・解像度変更時に確保し、毎フレーム `CreateCommittedResource` / `vkAllocateMemory` を繰り返していない。
- staging / upload bufferをpersistent mapして再利用している。
- pipelineは毎フレーム生成せず、Vulkanはpipeline cacheも使用している。
- compositorは3-slot ringを持ち、使用中slotに対してCPUを待たせず前フレームを再利用する設計になっている。
- Vulkanの通常render pathは1-frame-in-flightだが、これは巨大な共有intermediate bufferを複製しないための明示的なcorrectness要件であり、単純にframes-in-flightを増やすべきではない。
- normal presentationはCPU readbackされた画像ではなく、GPU上でcomposeされた高解像度outputを使用している。
- NVIDIA Reflex / AMD Anti-Lag / Intel XeLL / generic present pacingの待ちは低遅延制御として意図的に配置されており、それ自体をthroughput bugとは判定しない。

一方、静的に明確な最適化余地は存在する。

最も重要なのは次の2点である。

1. **Display Capture sidecarを有効scanlineごとにdispatchし、そのたびにbarrierを挟む構造**
2. **高解像度composed bufferを毎フレーム上画面・下画面のsampled textureへコピーしてからpresentする構造**

前者はDisplay Captureを多用するフレームでcommand/barrier数が増え、後者は内部解像度に対して **O(scale²)** でGPU転送量が増える。

さらに、Vulkan / DX12ともnative 256×192 capture用Resolve＋readback copyを通常3D frameでも記録しているが、structured 2D pathでは実際のCPU `GetLine()` が必要になるのはDisplay Captureが3D sourceを要求した場合だけである。この固定コストも削減可能である。

---

## 2. 監査対象

主に以下の実ファイルを、上記HEADのブランチ内容で確認した。

### Vulkan

- `src/GPU3D_Vulkan.h`
- `src/GPU3D_Vulkan.cpp`
- `src/GPU_Vulkan.cpp`
- `src/VulkanSync.cpp`
- `src/frontend/qt_sdl/MelonPrimeVulkanPresenter.cpp`

### DirectX 12

- `src/GPU3D_DX12.h`
- `src/GPU3D_DX12.cpp`
- `src/GPU_DX12.cpp`
- `src/DX12Context.cpp`
- `src/frontend/qt_sdl/MelonPrimeDX12SurfacePresenter.cpp`

### 共通 / structured 2D

- `src/GPU_Soft.cpp`
- `src/frontend/qt_sdl/CMakeLists.txt`

---

## 3. Finding一覧

| ID | 優先度 | Backend | Finding | 判定 |
|---|---:|---|---|---|
| PERF-001 | **P2 High** | Vulkan / DX12 | CaptureSidecarが有効scanlineごとにdispatch＋barrier | **確定** |
| PERF-002 | **P2 High** | Vulkan / DX12 | composed buffer→上/下screen textureの全画面copyがscale²で増加 | **確定** |
| PERF-003 | **P2 Medium** | Vulkan / DX12 | native Resolve＋readback GPU copyをcapture不要frameでも記録 | **確定** |
| PERF-004 | **P2 Medium** | DX12 | polygon batchごとにBinResultHeaderをIndirectArgsへcopy＋resource state往復 | **確定** |
| PERF-005 | **P3 Medium** | Vulkan / DX12 | structured 2D inputを毎frame約2.63 MiB CPU pack＋GPU copy | **確定** |
| PERF-006 | **P3 Low～Medium** | Vulkan / DX12 | steady-state descriptor write/allocationがまだ多い | **確定、実害は要計測** |
| PERF-007 | **P3 Measurement** | Vulkan / DX12 | raster frame開始時のprevious submission fence wait | **設計上意図的、実害は要計測** |

---

# 4. PERF-001 — CaptureSidecarのscanline単位dispatch＋barrier

## 優先度

**P2 High**

## 対象

### Vulkan

`src/GPU3D_Vulkan.cpp`

`VulkanRenderer3D::ComposeStructuredOutput()`

### DX12

`src/GPU3D_DX12.cpp`

`DX12Renderer3D::ComposeStructuredOutput()`

## 現状

両backendとも、192本のcapture commandをCPUで走査し、有効なlineごとに以下を実行している。

```text
for captureLine = 0..191
    if command is valid
        push/root constants update
        CaptureSidecar compute dispatch
        CaptureSidecar buffer/UAV barrier
```

Vulkanでは概ね次の形である。

```cpp
for (u32 captureLine = 0; captureLine < 192u; ++captureLine)
{
    if (...invalid...)
        continue;

    push.TexHeight = captureLine;
    vkCmdPushConstants(...);
    vkCmdDispatch(...);
    BufferBarrier(... CaptureSidecar ...);
}
```

DX12でも同じアルゴリズムで、各有効line後に `InsertUavBarrier(CaptureSidecarBuffer)` を行う。

## scale依存のcommand量

1 lineあたりのworkgroup数は、現行dispatch式から概ね以下になる。

```text
32 × scale × ceil(scale / 8)
```

192 lineすべてが有効だった場合:

| Internal Scale | Workgroups / line | 最大Workgroups / frame | Dispatch数 | Barrier数 |
|---:|---:|---:|---:|---:|
| 1x | 32 | 6,144 | 最大192 | 最大192 |
| 4x | 128 | 24,576 | 最大192 | 最大192 |
| 8x | 256 | 49,152 | 最大192 | 最大192 |
| 16x | 1,024 | 196,608 | 最大192 | 最大192 |

特に16xでは、CaptureSidecarだけで最大 **196,608 workgroups/frame** となる。

重要なのは単純なshader ALU量だけではなく、**最大192個の個別dispatchと最大192個のread/write barrier** がcommand streamへ入る点である。

## ボトルネックになる条件

- Display Captureが頻繁に有効になるゲーム / scene
- 1 frame内のvalid capture commandが多い
- 8x～16x internal resolution
- iGPU / UMA
- MoltenVKなどnative Vulkan以外のtranslation layer
- driverのsmall-dispatch / barrier overheadが大きい環境

## 推奨修正

単純にbarrierを全部削除してはいけない。

CaptureSidecarは過去のcapture結果を後続lineがsourceとして参照できる可能性があるため、line間dependencyの有無を保持する必要がある。

推奨は以下。

### A. valid lineをbatch化する

CPU側でvalid capture lineをcompact list化し、1 dispatchまたは少数dispatchへまとめる。

### B. shader側でcaptureLineをglobal invocationから算出する

192回の個別dispatchをやめ、Y方向へcapture lineを展開したdispatchへ変更する。

### C. dependencyが必要なlineだけbatch boundaryを切る

同一sidecar領域について

```text
line N write -> line N+1 read
```

のような真のRAW dependencyがある場合だけ同期する。

非alias lineまで一律global barrierする必要がない構造へ変更する。

### D. versioned / ping-pong sidecarも検討

capture sourceとdestinationをversion別に分ければ、lineごとのread-after-write dependencyそのものを減らせる可能性がある。

## 修正後の目標

通常frameについて:

```text
CaptureSidecarDispatchCount ≪ ValidCaptureLineCount
```

可能なら:

```text
CaptureSidecarDispatchCount = 1～数回 / frame
```

まで圧縮する。

---

# 5. PERF-002 — composed bufferからscreen textureへの高解像度全画面copy

## 優先度

**P2 High**

## 対象

### Vulkan

`src/frontend/qt_sdl/MelonPrimeVulkanPresenter.cpp`

`VulkanPresenter::UploadLayerFromBuffer()`

### DX12

`src/frontend/qt_sdl/MelonPrimeDX12SurfacePresenter.cpp`

`DX12SurfacePresenter::UploadLayerFromBuffer()`

## 現状

3D＋structured 2Dの最終結果は、Vulkan / DX12ともdevice-local **buffer** に生成されている。

presenterはそのbufferを直接sampleせず、上画面・下画面それぞれについて毎frame:

```text
composed GPU buffer
    ↓ full-screen copy
screen texture
    ↓ sample
present quad
```

という経路を通る。

### Vulkan

```text
compute-write buffer
 -> transfer-read barrier
 -> vkCmdCopyBufferToImage
 -> image SHADER_READ layout
 -> fragment shader sample
```

### DX12

```text
UAV buffer
 -> COPY_SOURCE
 -> CopyTextureRegion
 -> PIXEL_SHADER_RESOURCE texture
 -> pixel shader sample
```

## 転送量

1 screenのcomposed imageは:

```text
256 × scale × 192 × scale × 4 bytes
```

上＋下の2 screenで:

```text
393,216 × scale² bytes / frame
```

となる。

| Scale | 2画面copy / frame | 60fps時のcopy帯域 |
|---:|---:|---:|
| 1x | 0.375 MiB | 22.5 MiB/s |
| 4x | 6 MiB | 360 MiB/s |
| 8x | 24 MiB | 1.406 GiB/s |
| 16x | **96 MiB** | **5.625 GiB/s** |

これはpresent用screen copyだけの値であり、compositor自身のwrite、texture sampling、HUD/OSD upload等は含まない。

したがって16xでは、GPU帯域の大きいdesktop dGPUなら動作できても、**コピーだけで5.625 GiB/s** を消費する設計となる。

UMA / iGPU / MoltenVK環境では特に無視しにくい。

## 推奨修正

### 第一候補: compositorの最終出力を直接sample可能imageへする

現在の

```text
storage buffer -> copy -> sampled image
```

を

```text
storage image/UAV texture -> sampled imageとしてそのままpresent
```

へ変更する。

上画面・下画面を:

- 2枚のstorage+sampled image
- 2-layer array image
- 上下stackした1枚のimage

のいずれかで保持すればよい。

Vulkanではdevice feature probeでstorage-image対応formatを確認し、必要ならchannel swizzleまたはpresent shader側でformat差を吸収する。

DX12ではUAV textureとしてcompositorから直接writeし、presenterが同textureをSRVとして読む。

### 第二候補: bufferをpresent shaderから直接読む

buffer SRV / storage bufferをfragment shaderから直接読む方法もある。

ただし現在のradar描画ではlinear filteringを利用しているため、manual bilinearが必要になる。このため第一候補のsampleable image化を推奨する。

## 修正後の明確な成功条件

既存perf counterの

```text
PresentedScreenCopyBytes
```

が通常Vulkan / DX12 high-resolution presentで **0** になること。

これが最も分かりやすいacceptance criterionとなる。

---

# 6. PERF-003 — native 256×192 Resolve＋readback copyを毎3D frame記録

## 優先度

**P2 Medium**

## 対象

- `src/GPU3D_Vulkan.cpp`
- `src/GPU3D_DX12.cpp`
- `src/GPU_Soft.cpp`

## 現状

Vulkan / DX12とも3D `RenderFrame()`の最後で常にnative-resolution Resolveを実行する。

```text
Final high-resolution 3D
    ↓ Resolve compute
256×192 native ResolveBuffer
    ↓ GPU copy
readback buffer
```

1 frameあたりのreadback copyは:

```text
256 × 192 × 4 = 196,608 bytes
                  = 192 KiB
                  = 0.1875 MiB
```

60fpsでは約:

```text
11.25 MiB/s
```

なので帯域単体はPERF-002ほど大きくない。

しかしResolve dispatch、barrier/state transition、copy commandまで含めると完全にfreeではない。

## 重要な点

`src/GPU_Soft.cpp` のstructured 2D pathでは通常scanlineについて:

```cpp
Output3D = structuredVulkan2D
    ? Structured3DPlaceholderLine
    : Rend3D->GetLine(line);
```

となっている。

つまりstructured Vulkan / DX12では、通常の2D compositionのために毎scanline native 3D readbackを要求していない。

実際に `Rend3D->GetLine()` が必要になるのはDisplay Captureが3Dをsourceとして必要とする場合である。

そのため現状は:

```text
通常frame
  GPU Resolve + GPU readback copy = 実行
  CPU fence wait / map / memcpy    = 通常は未実行
```

となっている。

## CPU stallについて

### Vulkan

`EnsureFrameReadback()`は最初の`GetLine()`まで待ちを遅延し、current frameのfenceだけを `vkWaitForFences()` する。

`vkDeviceWaitIdle()`ではない。

### DX12

`EnsureFrameReadback()`は最初の`GetLine()`で `Commands.WaitIdle()` を行い、その後readback resourceをMap→memcpy→Unmapする。

これもDisplay Captureで3D sourceが必要なframeに限って表面化する。

## 推奨修正

native Resolve / readbackを **lazy capture path** へ分離する。

安全性を考えると、単純に`RenderFrame()`開始時の`GPU.CaptureEnable`だけで省略判断するのは避ける。

DS registerはframe途中で変更される可能性があるためである。

より安全なのは:

```text
RenderFrame
  high-resolution FinalFBまでは生成
  native Resolve/readbackは未実行

最初のcapture GetLine要求
  ↓
同じGPU queueへnative Resolve + readback copy commandを追加submit
  ↓
その専用fenceだけ待つ
  ↓
CPU readback
```

というlazy submissionである。

同一queueのsubmission orderにより、前のFinalFB writeとの順序は維持できる。

## 期待効果

Display Captureが3D sourceを必要としない大多数のframeで:

- Resolve dispatch削除
- Resolve barrier削除
- 192 KiB GPU→readback copy削除
- host visibility barrier / resource transition削除

が可能になる。

---

# 7. PERF-004 — DX12 BinResultHeader→IndirectArgs copy

## 優先度

**P2 Medium**

## 対象

`src/GPU3D_DX12.cpp`

`DX12Renderer3D::RenderFrame()`

## 現状

各polygon batchでCalcOffsetsの後に:

```text
BinResultBuffer
  UAV -> COPY_SOURCE
  ↓
CopyBufferRegion(sizeof(BinResultHeader))
  ↓
IndirectArgsBuffer
  COPY_DEST -> INDIRECT_ARGUMENT
  ↓
ExecuteIndirect(SortWork)
ExecuteIndirect(Rasterise variants...)
  ↓
IndirectArgsBuffer -> COPY_DEST
BinResultBuffer -> UAV
```

という経路になっている。

`BinResultHeader`は:

```text
MaxVariants = 2048
sizeof(BinResultHeader)
  = 2048 × 24 + 16
  = 49,168 bytes
```

約48 KiBである。

1回のcopy量は大きくないが、**polygon batchごと**に:

- BinResult全体のstate transition
- 約48 KiB copy
- IndirectArgs transition

が追加される。

## 根本原因

CalcOffsetsがindirect argumentをBinResult側へ生成し、D3D12では同resourceを同時にUAVとINDIRECT_ARGUMENTとして扱えないため、一旦別bufferへcopyしている。

## 推奨修正

CalcOffsets shaderから、最初から専用の`IndirectArgsBuffer` UAVへdispatch argumentを書き込む。

```text
CalcOffsets
  ↓ UAV write
IndirectArgsBuffer
  UAV -> INDIRECT_ARGUMENT
  ↓
ExecuteIndirect
  ↓
INDIRECT_ARGUMENT -> UAV
```

へ変更する。

これにより:

- `BinResultBuffer` のUAV→COPY_SOURCE→UAV往復を削除
- `CopyBufferRegion(49,168 bytes)` を削除
- copy-source dependencyを削除

できる。

`IndirectArgsBuffer`は `ALLOW_UNORDERED_ACCESS` を付け、shader layoutを現在のindirect command layoutと一致させる。

---

# 8. PERF-005 — structured input 2.63 MiB/frame pack＋copy

## 優先度

**P3 Medium**

## 対象

- `VulkanRenderer3D::ComposeStructuredOutput()`
- `DX12Renderer3D::ComposeStructuredOutput()`
- `StructuredComposition::PackRoutedScreenPlanes()` 周辺

## 現状

structured inputは:

```text
14 × 256 × 192 pixel planes
+ 2 × 192 line metadata
+ 192 × 4 capture command words
```

である。

総量は:

```text
689,280 u32
= 2,757,120 bytes
= 約2.629 MiB / frame
```

60fpsではstaging destinationへのwriteだけでも約:

```text
157.8 MiB/s
```

となる。

その後さらにGPU側device-local structured inputへcopyしている。

これはscaleには依存しないが、CPU cache / memory bandwidthの小さい環境では一定の固定費になる。

## 推奨修正候補

### 1. dirty plane / dirty line tracking

前frameから変化していないcapture/provenance planeまで毎回全copyしない。

### 2. route-run単位copyの拡張

既にscreen routingにはrunの概念が存在するため、実際に変化したrunだけpackする。

### 3. producerからstaging slotへ直接書く

将来的にはSoftRendererが192 scanlineを生成する段階で、そのframeのcompositor staging slotへ直接structured recordを書ければ、VBlankでの二重packを削減できる。

ただし3-slot ownershipとrenderer切替時のlifetimeを壊さない設計が必要になる。

---

# 9. PERF-006 — descriptor write/allocation overhead

## 優先度

**P3 Low～Medium**

## DX12

raster frameごとに少なくとも:

- 12 UAV descriptorを`CreateUnorderedAccessView`
- static SRV 5個をshader-visible heapへcopy
- unique textureごとにSRV descriptor

を生成している。

compositor slotでも12 UAV descriptorを毎compose時に生成する。

presenterもdraw layerごとにSRV descriptorを生成する。

既にframe-local texture cacheがあるため同一textureの重複writeは抑えられているが、固定resourceのUAVまで毎frame書き直す必要性は低い。

## Vulkan

raster texture descriptor setはframe内cacheが存在するため悪くない。

ただしpresenterはlayer drawごとに:

```text
vkAllocateDescriptorSets
vkUpdateDescriptorSets
```

を実行する。

per-frame pool reset方式なのでメモリリーク等の問題はないが、CPU overheadは残る。

## 推奨修正

優先順位はPERF-001～004より低い。

DX12ではscale changeまで不変のUAV/SRV tableを固定slotへprebuildし、frame ringで上書きされない領域へ分離する。

compositorは3 output slotごとに固定tableを保持できる。

Vulkan presenterも各layer image/sampler組についてdescriptor setをframe slot単位でpreallocateし、image再生成時だけupdateする構成を検討できる。

---

# 10. PERF-007 — raster 1-frame-in-flight / fence wait

## 優先度

**P3 Measurement**

## Vulkan

`GPU3D_Vulkan.h`では:

```cpp
static constexpr u32 RendererFramesInFlight = 1;
```

である。

理由もコードに明記されており、以下の巨大なintermediate resourceを全frameで共有しているためである。

- XSpanSetups
- tile buffers
- BinResult
- WorkDescs
- ResultBuffer
- FinalFB

特に16xではtile bufferが非常に大きく、単純なdouble/triple bufferingはVRAMを大量消費する。

`VulkanSync::FrameRing::BeginFrame()`はslotにpending submissionがあれば、そのslot fenceを待ってcommand poolをresetする。

これは設計上正しい。

## DX12

`DX12CommandContext`も1 command allocator / 1 command list / 1 fenceを持ち、`Begin()`で前回`SubmittedValue`を待ってallocatorをResetする。

したがってmain raster pathは実質的に同様のserial resource reuseになっている。

## 判定

**静的監査だけではこれをperformance bugとは判定しない。**

低遅延志向ではqueue depthを増やさないこと自体に利点がある。

またVulkan側コメントの通り、DS software 2D処理がframe N-1 GPU処理とframe N開始waitの間に走るため、実際のwait時間はかなり隠蔽される可能性がある。

## 既存計測ポイント

両backendに`RasterBeginWait`計測が存在する。

したがって先に実測する。

### 修正を検討する条件

```text
RasterBeginWait p99 が継続的に有意
```

である場合のみ最適化対象とする。

### やってはいけない修正

```text
RendererFramesInFlight = 2 / 3 にするだけ
```

これは共有intermediate bufferのWAR/WAW raceを発生させるため不可。

改善するなら:

- CPU-only geometry preparationのうちGPU slot ownershipを必要としない部分をwaitより前へ移す
- resource dependencyを細分化する
- 小さいresourceだけframe-local化する

など、VRAM増加を抑えた方法を使う。

---

# 11. Present pacing / low-latency待ちについて

## DX12

swapchainは:

```text
BufferCount = 2
SetMaximumFrameLatency(1)
DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
```

を使用している。

policyによって`WaitForPresentSlot()`を使い、その後present command contextのfence reuse待ちが入る。

これはNVIDIA Reflex / AMD Anti-Lag 2 / Intel XeLL / host pacingとのauthority切替を含む低遅延設計であり、**静的にはボトルネックfindingにしない**。

既に:

- `PresentSlotWait`
- `PresentBeginWait`

が別metricとして存在するため、二重waitが実際に時間を消費しているか実測可能である。

## Vulkan

`AcquireNextImageKHR()`、frame-slot fence、swapchain-image fenceが分離され、QueuePresent周辺もqueue mutexの内外を明示的に整理している。

Reflex / Anti-Lag marker範囲にmutex contentionを混ぜない構成も適切である。

この部分は今回の静的監査では修正対象としない。

---

# 12. 良好だった実装

今回の監査で、performance上明確に良い点も確認した。

## Vulkan

- `FrameStaging` persistent mapped ring
- `MetaUniformBuffer` persistent map
- texture upload ring
- deferred destruction
- FinalFBをlifetime中`GENERAL`に維持し不要なlayout往復を削減
- pipeline cache保存/再利用
- incremental pipeline compile
- identical 3D frameでdispatch/uploadをskip
- compositorの`TryBeginFrame()`によるnon-blocking frame drop
- swapchain resize / resource recreate以外ではdevice-wide idleを使わない
- capture CPU readback fence waitを`GetLine()`まで遅延

## DX12

- 32 MiB persistent upload ring
- persistent mapped staging resources
- texture SRV frame cache
- identical frame skip
- compositor 3-slot ring＋`TryBegin()`
- resource createをinit/resize/scale-changeへ限定
- GPU validationをrelease hot pathから除外
- native presenterがGPU上のcomposed outputを入力にしている

これらは維持するべきである。

---

# 13. 推奨修正順序

## Track A — 高解像度時のthroughput改善

### 1位: PERF-002

compositor outputを直接sample可能なGPU imageに変更し、上/下画面のbuffer→texture copyを除去する。

特に8x～16xへの効果が大きい。

### 2位: PERF-005

structured inputのfull pack量を減らす。

## Track B — Display Capture負荷改善

### 1位: PERF-001

CaptureSidecarのline単位dispatch/barrierをbatch化する。

### 2位: PERF-003

native Resolve/readbackをcapture-demand型へ変更する。

## DX12固有

### 3位: PERF-004

CalcOffsetsからIndirectArgsBufferへ直接UAV writeする。

## 最後

### PERF-006 / PERF-007

descriptor churnとraster begin waitはprofiling結果を見てから最適化する。

---

# 14. 実測監査プラン

静的監査で「処理が存在する」ことは確定できるが、GPU別にframe timeの何％を占めるかはruntime timestampが必要である。

現状すでに`VulkanPerf` / `DX12Perf`が多数のCPU metric / counterを持っているため、以下を追加すれば十分である。

## 追加推奨counter

```text
CaptureValidLineCount
CaptureSidecarDispatchCount
CaptureSidecarBarrierCount
NativeResolveCount
NativeReadbackCopyBytes
CompositorGpuTimeNs
CaptureSidecarGpuTimeNs
PresentedScreenCopyGpuTimeNs
DX12IndirectArgsCopyBytes
DX12IndirectArgsCopyCount
```

## A/B条件

内部解像度:

```text
1x
4x
8x
16x
```

Display Capture density:

```text
0 valid lines
1 valid line
32 valid lines
192 valid lines
```

VSync / low-latency:

```text
VSync ON
VSync OFF
Reflex OFF/ON/BOOST
AMD Anti-Lag OFF/ON
Intel XeLL OFF/ON（対応環境）
```

低遅延技術を有効にしたtestでは、意図的なlate-wait時間と純粋なGPU render timeを混同しないこと。

---

# 15. 修正後acceptance criteria

## PERF-001

- CaptureSidecar dispatch数がvalid line数に1:1比例しない。
- 必要なline dependencyを維持する。
- Software/OpenGL ComputeとのDisplay Capture結果が一致する。

## PERF-002

- 通常Vulkan / DX12 presentで `PresentedScreenCopyBytes == 0` を目標にする。
- 1x～16xで画質・screen routing・radar・HUD・OSDが変化しない。

## PERF-003

- Display Captureで3D sourceを使わないframeでは `NativeReadbackCopyBytes == 0`。
- 3D sourceを使うcapture frameではSoftware/OpenGL Computeと一致する。
- mid-frame capture register changeを壊さない。

## PERF-004

- DX12でBinResultHeaderの`CopyBufferRegion`が消える。
- IndirectArgsのみUAV↔INDIRECT_ARGUMENT transitionする。
- 全variant / polygon batchで出力一致。

## 全体

- NVIDIA Reflex / AMD Anti-Lag 2 / Intel XeLLのmarker/pacing順序を変更しない。
- Vulkan presenterのqueue mutex / marker boundaryを変更しない。
- frames-in-flightを安易に増やさない。
- 1x rendering accuracyを悪化させない。
- high-resolution structured compositorの画質を悪化させない。

---

# 16. 最終判定

## P1

**なし。**

steady-stateで毎frame device-wide idleする構造、毎frame pipeline/resourceを再生成する構造、CPU readbackをnormal presentationの必須経路にする構造は確認できなかった。

## P2

**4件。**

中でも優先度が高いのは:

1. `PERF-001 CaptureSidecar scanline dispatch/barrier amplification`
2. `PERF-002 composed buffer -> screen texture copy`

である。

`PERF-002`は内部解像度scale²で転送量が増えるため、特に8x～16xで改善効果を期待できる。

`PERF-001`はDisplay Capture使用量に依存するが、capture-heavy frameではdispatch/barrier overheadが大きくなる可能性が高い。

## P3

**3件。**

structured pack、descriptor churn、frame-begin fence waitは次段階のprofiling対象とする。

---

## 17. 監査スナップショット

この文書の静的監査対象は以下で固定する。

```text
Repository : ag-advania/melonPrimeDS
Branch     : develop_remakeVulkan_ver3
Commit     : 6cd32fc34d4a41d7797c5823ee62ca11a9efc467
Tree       : 02382dcbb6cda646ae4896460a5cc89420ed75ae
```

以後branchへcommitが追加された場合は、上記commitとの差分を対象に再監査する。
