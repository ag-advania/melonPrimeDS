# melonPrimeDS `develop_metal` 完全Metal化 実行指示書

**再作成日:** 2026-07-17  
**対象リポジトリ:** `https://github.com/ag-advania/melonPrimeDS`  
**対象ブランチ:** `develop_metal`  
**基準HEAD:** `17b46586c24cf93594be7ffd2d14857a0e5669e0`  
**基準コミット:** `metal: publish OutputState atomically and harden capture writers`  
**比較元:** `develop`  
**developとの差:** 10コミット先行、0コミット遅延  
**対象OS:** macOS  
**対象renderer:** `Metal`／`Metal Compute Shader`  
**目的:** 通常フレームのDS 3D、DS 2D、Display Capture、最終合成、HUD／OSD、presentをGPU常駐Metal経路へ統合し、Software renderer由来のCPU pixel合成、通常フレームreadback、CPU BGRA presentation、Metal ComputeからMetal Rasterへの暗黙fallbackを撤廃する。  
**本書の位置付け:** `17b46586`時点の実装を唯一の前提とし、過去の未修正状態を再実装しないための最新版実行指示書。  

---

# 0. 本書の最重要指示

## 0.1 前回指示からの変更点

前回の指示書で最優先としていた次の項目は、基準HEADでコード上実装済みである。

- `OutputState`のatomic `shared_ptr` publication
- `LoadOutputState()`／`StoreOutputState()`／`ExchangeOutputState()`
- `GetOutput()`内のmember再読込撤廃
- immutable `MetalOutputState` swap
- `MetalOutputState` destructorの無期限wait撤廃
- GPU capture複数in-flight count
- GPU capture monotonic token
- stale GPU completionのfinalize拒否
- capture pending layerのO(1) deduplicate
- texture pixel format／sample count／mipmap／depth validation
- producer変更時に新outputをvalidateしてから旧leaseを解放
- ordering reject後のgeneric invalid log抑制
- producer変更時の`lastPresentedFrame` reset

これらを新たに作り直してはならない。

最初の作業は再実装ではなく、次の検証である。

```text
1. 最新修正をTSan／実機buildで確定する
2. output contractの残りを小さく仕上げる
3. M4 Display Captureのdifferential validation scaffoldを作る
4. capture frameをper-scanline／per-segment Full-GPUへ移す
```

## 0.2 現在の主戦場

現在の完全Metal化を阻む最大の構造的残件は、Display Captureである。

現行Full-GPU適格性判定は、次のcapture requestを事前除外する。

```cpp
if (GPU.CaptureCnt & (1u << 31))
    return false;
```

このためcapture frameでは:

```text
Metal 3D
→ native readback
→ SoftRenderer 2D
→ CpuBgra
→ Metal presenterへupload
```

へ戻る。

したがって、次の実装開始地点はM4である。

> **実DSのscanline単位Display Capture因果をMetal frame graphへ実装し、capture frameでもFull-GPU出力を成立させる。**

## 0.3 現時点で完全Metalと呼んではならない理由

基準HEADでも次が残る。

- `MetalRenderer : public SoftRenderer`
- capture frameの`SoftRenderer::VBlank()`
- Metal rendererの`AcquireOutputLease()`末尾で`SoftRenderer::GetOutput()`
- Metal presenterの`CpuBgra`受理
- presenterの`screenTex` CPU upload
- `QImage uiOverlay`
- `QImage bottomImage`
- Custom HUD／OSDのCPU描画
- `MetalComputeRenderer3D::RasterReference`
- `MetalComputeRenderer3D::GetLine()`
- OpenGL初回既定
- embedded MSL source compile
- 高scale capture textureの巨大memory
- 最新HEADに紐づくCI status／workflow evidenceなし

---

# 1. 完全Metal化の定義

## 1.1 完了A: emulator GPU pathの完全Metal化

Metal選択中の通常フレームについて、次をすべてMetal上で実行する。

- DS 3D rasterization
- DS 3D texture decode
- texture cache
- depth
- stencil
- polygon attribute
- translucent polygon
- shadow
- fog
- edge marking
- DS 2D BG
- DS 2D OBJ
- window
- mosaic
- priority
- alpha blend
- color effect
- master brightness
- screen swap
- Display Capture
- capture-backed BG
- capture-backed OBJ
- capture-backed 3D texture
- internal resolution scaling
- final two-screen texture array
- renderer output lease
- presenter draw
- `CAMetalLayer` present

完了Aでは通常フレームの次を禁止する。

```text
SoftRenderer::DrawScanline
SoftRenderer::DrawSprites
SoftRenderer::VBlank
SoftRenderer::GetFramebuffers
ReadbackNativeColorTargetToLineBuffer
RendererOutputKind::CpuBgra from MetalRenderer
CPU完成画面のreplaceRegion
screenTexへのtop／bottom upload
Metal ComputeからRasterReference.RenderFrame
```

限定readbackを許可する操作:

- ARM CPUがcapture-backed VRAMを読む
- savestate
- screenshot
- video capture
- renderer切替
- debug differential comparison
- explicit diagnostic command
- shutdown drain

## 1.2 完了B: end-to-end通常フレーム完全Metal化

完了Aに加えて、次をMetal commandで描画する。

- Custom HUD
- radar
- HUD editor
- OSD
- splash
- icons
- text
- aim reticle
- debug overlay

禁止:

```text
毎フレームQImage全体clear
毎フレームQPainter全体合成
毎フレームbottom framebuffer memcpy
毎フレームUI texture全面upload
毎フレームglyph bitmap再生成
```

許可:

- glyph atlas初回生成
- font／language変更時のatlas更新
- static icon atlas生成
- HUD stateからdraw commandをCPUで作る処理
- dirty command buffer更新
- explicit screenshot用readback

## 1.3 完全Metal化に含めないもの

次はCPUのままでよい。

- ARM7／ARM9 emulation
- GPU register decode
- VRAM mapping解析
- polygon list構築
- 2D register snapshot
- frame graph構築
- command encoding
- input
- audio
- Wi-Fi
- ROM patch
- Qt設定画面
- savestate serialization
- explicit readback request

完全Metal化とは、アプリ全体をGPU化することではない。

> 通常フレームの可視pixel生成と表示をGPU resident Metal pathで完結させること。

---

# 2. 最新HEADの実装状況

## 2.1 phase status

| Phase | 状態 | `17b46586`時点の評価 |
|---|---|---|
| M0 診断 | 概ね完了 | strict／perf／visible source診断あり。CI自動化とmemory telemetryは不足 |
| M1 output contract | 大幅完了 | lease、metadata、ProducerId、Generation、FrameSerial、atomic publication、fail-closed validationあり |
| M2 shared Metal context | 完了 | renderer／presenterの共有device基盤あり |
| M3 Full-GPU segmented 2D | 完了 | captureなし適格frameでproduction既定 |
| M4 Display Capture Full-GPU | 未完了 | capture requestを適格性判定で除外 |
| M5 normal readback 0 | 部分完了 | captureなしframeは進展。capture CPU fallbackとexplicit readbackが残る |
| M6 SoftRenderer撤廃 | 未着手 | `MetalRenderer : public SoftRenderer` |
| M7 Compute独立化 | 実質部分完了 |通常sceneでCompute cutover可能。`RasterReference`と`GetLine()`が残る |
| M8 presenter texture-only | 部分完了 | Metal texture直接表示可能。CpuBgra／screenTexが残る |
| M9 HUD／OSD Metal化 | 未完了 | dirty-rect最適化済みだがQImage／QPainter path |
| M10 macOS初回Metal既定 | 未着手 | `3D.Renderer=OpenGL` |
| M11 shader asset分離 | 未着手 | embedded source＋`newLibraryWithSource` |
| M12 CI／release gate | 未完了 | 最新HEADのstatus／workflow evidenceなし |

## 2.2 最新commitで完了した項目

### OutputState

```cpp
std::shared_ptr<MetalOutputState> LoadOutputState() const;
void StoreOutputState(std::shared_ptr<MetalOutputState>);
std::shared_ptr<MetalOutputState>
ExchangeOutputState(std::shared_ptr<MetalOutputState>);
```

atomic free functionsでpublicationを行う。

全readerはlocal snapshotを使用する方向へ修正済み。

### output state reconfigure

scale変更時:

```text
新stateを完全構築
→ allocation成功
→ atomic exchange
→ old stateはlease／completion ownershipで自然drain
```

旧stateの`PresenterRefCount==0`を同期waitしない。

### output validation

presenterで次を検証する。

- texture non-null
- device一致
- `MTLTextureType2DArray`
- array length 2
- `BGRA8Unorm`
- sample count 1
- mipmap level 1
- depth 1
- width
- height
- scale
- generation
- frame serial
- producer ID
- 256×scale
- 192×scale

### capture writer

現在:

```cpp
uint32_t GpuWritesInFlight;
uint64_t LatestGpuSubmittedToken;
uint64_t NextGpuWriteToken;
```

を持つ。

古いcompletion:

```text
in-flight countだけ減らす
Valid／Dirtyを変更しない
```

latest tokenだけがfinalizeする。

### presenter transition

producer変更時:

```text
new texture validation
→ valid
→ old lease release
→ new lease move
```

の順。

新producerのinvalid first frameでlast-known-goodを先に失わない。

---

# 3. コード境界

## 3.1 MelonPrime guard

melonDS共通コードを変更する場合:

```cpp
#ifdef MELONPRIME_DS
// MelonPrime専用
#endif
```

Metal専用:

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_METAL)
// Metal専用
#endif
```

Apple native API:

```cpp
#if defined(__APPLE__) && \
    defined(MELONPRIME_DS) && \
    defined(MELONPRIME_ENABLE_METAL)
// Objective-C++／Metal／QuartzCore
#endif
```

## 3.2 Objective-C型を共通headerへ漏らさない

禁止:

```cpp
id<MTLTexture> Texture;
```

を`GPU.h`等へ直接置く。

使用:

- opaque `void*`
- numeric metadata
- PImpl
- Objective-C++ `.mm`内部型

## 3.3 Software／OpenGLを壊さない

変更禁止:

- Software rendererの正常path
- OpenGL rendererの正常path
- OpenGL presenter
- Windows／Linux default
- upstream melonDSの出力contract
- 非Metal build時のheader parse

共通API変更時は、Software／OpenGL既存動作を維持する。

---

# 4. 実装原則

## 4.1 最新修正を逆戻りさせない

禁止:

- atomic load／exchangeをplain copyへ戻す
- immutable stateをin-place再構成へ戻す
- presenter refを同期waitする
- GPU capture countをboolへ戻す
- writer tokenを削除する
- producer transition前に旧leaseを解放する
- validationをfail-openへ戻す
- destructorに無期限waitを戻す

## 4.2 frame単位fallbackを完成扱いしない

現行CPU fallbackは互換path。

完全Metal判定:

```text
Metal:
    MetalTexture = normal
    CpuBgra = violation

Metal Compute:
    Compute final texture = normal
    RasterReference = violation
```

## 4.3 canonical capture stateとenhancement cacheを分ける

Display Captureの正解状態はDS native pixelである。

推奨:

```text
Canonical capture:
    native resolution
    packed RGB5551
    line-precise generation
    CPU VRAM syncのauthority

Enhanced cache:
    optional high-resolution derivative
    GPU-only
    sampling quality向上用
    correctness authorityにしない
```

scale-expanded全layer textureをcanonical stateにしない。

## 4.4 writer orderingは2軸で管理する

```text
DirtySerial:
    content／mapping／sizeの世代

WriteToken:
    submission順序
```

completion acceptance:

```text
command success
AND submitted DirtySerial == current DirtySerial
AND token == latest authoritative token
AND generation一致
```

## 4.5 normal frameで同期waitしない

禁止:

```cpp
waitUntilCompleted();
Completion.wait();
dispatch_semaphore_wait(... forever);
```

通常frame pathでは使用しない。

許可:

- ARM CPU read
- savestate
- screenshot
- video capture
- renderer switch
- shutdown
- debug validation

## 4.6 memoryをphaseごとに計測する

必須counter:

- private texture bytes
- shared buffer bytes
- old/new overlap
- staging capacity
- scratch capacity
- one-off allocation
- process RSS
- Metal allocated size
- allocation failure
- scale transition peak

---

# 5. 実装順序

最新版の推奨PR順:

```text
PR-0  最新修正のvalidation gate
PR-1  output contract最終仕上げ
PR-2  capture differential validation scaffold
PR-3  native canonical capture storage
PR-4  per-scanline／per-segment capture scheduler
PR-5  capture Full-GPU production cutover
PR-6  normal readback 0
PR-7  SoftRenderer継承撤廃
PR-8  Metal Compute RasterReference撤廃
PR-9  presenter MetalTexture-only
PR-10 radar native Metal
PR-11 HUD primitive renderer
PR-12 glyph atlas／OSD／splash
PR-13 macOS初回Metal既定
PR-14 MSL asset／metallib
PR-15 CI／release gate
```

1 commitへ複数PR内容を混ぜない。

---

# 6. PR-0: 最新修正のvalidation gate

## 6.1 目的

`17b46586`のatomic publication、capture writer token、presenter transitionを実機とsanitizerで確定する。

新機能を追加しない。

## 6.2 対象確認

- `src/GPU_Metal.h`
- `src/GPU_Metal.mm`
- `src/GPU_MetalCaptureMethods.inc`
- `src/GPU_MetalFullGpuMethods.inc`
- `src/frontend/qt_sdl/MelonPrimeScreenMetal.mm`

## 6.3 static check

grepで確認:

```text
OutputState direct copy／assign = 0
OutputState std::exchange = 0
LoadOutputState経由
ExchangeOutputState経由
GetOutput raw MetalTexture = 0
GpuCaptureInFlight bool = 0
GpuWritesInFlight使用
LatestGpuSubmittedToken使用
```

許可される`OutputState` direct referenceはhelper実装内だけ。

## 6.4 build

必須:

- macOS ARM64 Metal ON
- macOS Intel Metal ON
- macOS Metal OFF
- macOS FORCE_DISABLE
- Windows
- Linux

## 6.5 TSan stress

操作:

```text
1x→2x→4x→8x→16x→8x→4x→2x→1x
```

1000 cycle。

同時に:

- fullscreen
- resize
- Retina display移動
- pause／resume
- fast-forward
- VSync切替
- Metal↔Metal Compute
- ROM close／open
- savestate
- app shutdown
- multi-instance

合格:

```text
OutputState data race 0
CaptureMeta data race 0
use-after-free 0
deadlock 0
stale lease 0
negative ref count 0
negative in-flight count 0
```

## 6.6 capture callback ordering test

意図的に複数commandをin-flightにする。

```text
A submit
B submit
A completion
B completion
```

```text
A submit
B submit
B callback observed first
A callback observed later
```

```text
GPU submit
AllocCapture
GPU completion
```

合格:

- old tokenがValidを変更しない
- countが正しく0へ戻る
- Dirtyが消失しない
- latest tokenだけがfinalize

## 6.7 完了条件

PR-0はコード変更なしでもよい。

提出物:

- build log
- TSan log
- scale stress log
- callback test log
- memory peak
- unresolved failure一覧

PR-0が通るまでM4実装へ進まない。

---

# 7. PR-1: output contract最終仕上げ

## 7.1 残件

最新HEADで実texture validationは強化済み。

残る項目:

1. `RendererOutput`側にpixel format identifierがない
2. Metal `AcquireOutputLease()`が最後にCpuBgraを返す
3. lease contextのper-frame heap allocation
4. output fault injectionが自動化されていない
5. producer／generation telemetry不足
6. raw `GetOutput()`がCpuBgra互換path
7. latestHEADのCI evidenceなし

## 7.2 PixelFormat metadata

共通C++型:

```cpp
enum class RendererPixelFormat : u32
{
    Unknown = 0,
    Bgra8Unorm = 1,
};
```

Metal output:

```text
metadata = Bgra8Unorm
actual texture = MTLPixelFormatBGRA8Unorm
```

presenterは両方検証する。

Objective-C enumを`GPU.h`へ公開しない。

## 7.3 Metal output Kind

M4完了前:

```text
MetalTexture
CpuBgra fallback
```

を維持してよい。

ただしfallback reasonを明示する。

追加候補:

```cpp
enum class RendererOutputFallbackReason
{
    None,
    Startup,
    CaptureFrame,
    FullGpuInvalidated,
    MetalCommandFailure,
    ResourceUnavailable,
};
```

strict summaryへreason countを出す。

M4完了後にCpuBgraを削除する。

## 7.4 fault injection

debug-onlyで次を生成する。

- nil texture
- wrong device
- wrong type
- wrong array length
- wrong pixel format
- sample count mismatch
- mipmap mismatch
- width mismatch
- height mismatch
- scale mismatch
- generation 0
- serial 0
- producer 0
- generation rollback
- frame serial rollback
- producer transition invalid first frame

全てfail-closed。

## 7.5 lease context allocation

correctness確定後に最適化。

候補:

- fixed-size pool
- slot embedded lease token
- freelist
- small object cache

1PRでcontract変更とallocation最適化を混ぜない。

## 7.6 完了条件

- PixelFormat metadata一致
- fault injection全PASS
- validation failureでtexture sampling 0
- producer transition blankなし
- lease leakなし
- Software／OpenGL回帰なし

---

# 8. PR-2: Display Capture differential validation scaffold

## 8.1 目的

capture GPU pathをproduction表示へ直結する前に、既知正解のCPU結果と自動比較できる環境を作る。

## 8.2 feature flag

debug／developer専用:

```text
MELONPRIME_METAL_CAPTURE_EXPERIMENT=1
```

動作:

```text
CPU referenceを表示
GPU capture candidateも生成
candidateは画面へ出さない
checksum／pixel diffを記録
```

昇格後はenablement依存を削除する。

## 8.3 比較対象

- capture destination native RGB5551
- top final 256x192
- bottom final 256x192
- per-line checksum
- per-segment checksum
- source A
- source B
- A+B blend
- capture-backed source
- CPU VRAM readback結果

## 8.4 mismatch metadata

必須:

```text
frame serial
line
x
captureCnt
dispCntA
source A mode
source B mode
destination bank
destination offset
capture size
EVA
EVB
FIFO flag
source offset
screen swap
brightness
capture read generation
capture write generation
GPU writer token
first mismatch
max channel delta
mismatch count
```

## 8.5 artifact

debug時:

```text
reference-native.rgb5551
metal-native.rgb5551
reference.png
metal.png
diff.png
frame.json
capture-lines.csv
commands.txt
```

通常buildでは出力しない。

## 8.6 test matrix

### capture size

- 128x128
- 256x64
- 256x128
- 256x192

### source

- source A 2D
- source A 3D
- source B VRAM
- source B FIFO
- source B capture-backed
- A+B

### offsets

- destination 0／1／2／3
- source offset 0／1／2／3
- signed 3D X offset

### state change

- line途中CaptureCnt変更
- line途中DispCnt変更
- VRAM mapping変更
- screen swap変更
- brightness変更
- BGがcapture destinationを参照
- OBJがcapture destinationを参照
- 3D textureがcapture destinationを参照

## 8.7 homebrew test

可能ならcapture専用test ROMを追加する。

1 testにつき期待checksumを固定する。

実ROMだけへ依存しない。

## 8.8 完了条件

- CPU referenceとGPU candidateを同frameで比較可能
- mismatch lineを特定可能
- production表示挙動不変
- test matrix自動化
- artifact生成可能

---

# 9. PR-3: native canonical capture storage

## 9.1 目的

scale²で巨大化する現行capture arraysを、DS native RGB5551 authorityへ置き換える。

## 9.2 現行memory問題

scale 16:

```text
Capture128   約256 MiB
Snapshot128  約256 MiB
Capture256   約256 MiB
Snapshot256  約256 MiB
合計         約1 GiB
```

scale reconfigureでは旧stateと新stateが共存する。

8x→16xでcaptureだけでも約1.25 GiB。

## 9.3 canonical format

推奨:

```text
MTLPixelFormatR16Uint
```

内容:

```text
RGB5551 packed
native resolution
exact DS capture state
```

resources:

```text
Capture128Native:
    128x128
    16 layers
    R16Uint

Capture256Native:
    256x256
    4 layers
    R16Uint
```

合計約1 MiB。

snapshotを同数持っても約2 MiB。

## 9.4 shader decode

共通MSL helper:

```metal
float4 mp_unpack_rgb5551(ushort value);
ushort mp_pack_rgb5551(float4 value);
```

capture blendは可能な限りinteger DS semanticsで実装する。

浮動小数RGBA8をauthorityにしない。

## 9.5 EnhancedCaptureCache

高解像度capture再利用を維持する場合、canonical textureとは別にする。

```text
Canonical:
    native R16Uint
    correctness authority

Enhanced:
    optional
    GPU-only
    current scale
    active layerだけ
    lazily generated
    discardable cache
```

Enhanced cacheがなくても正しく描画できること。

## 9.6 active-layer cache

全20 layerをscale-expandedで保持しない。

候補:

- 2～4 slot LRU
- per-frame needed layer set
- generation key
- bank／block／size／scale key
- stale cache discard

key:

```cpp
struct CaptureCacheKey
{
    u8 Bank;
    u8 Start;
    u8 Size;
    u32 Scale;
    u64 Generation;
};
```

## 9.7 CPU upload

現行:

```text
CPU RGB5551
→ CPUでscale² RGBA8展開
→巨大shared buffer
→blit
```

変更:

```text
CPU RGB5551 native
→ small R16Uint upload
→ canonical texture
```

CPU scratch:

```text
最大256x256x2 = 128 KiB
```

へ抑える。

## 9.8 CPU readback

canonical R16Uintから直接native bufferへreadback。

compute decode不要。

`ReadbackBuffer`はnative size。

## 9.9 acceptance

- CPU reference diff 0
- scale 1～16でcanonical内容同一
- scale変更でcapture state再生成不要
- capture steady memoryがscale非依存
- 16x one-off 64 MiB staging 0
- explicit CPU readback parity

---

# 10. PR-4: per-scanline／per-segment capture scheduler

## 10.1 根本問題

実DS:

```text
line Nの2D／3D生成
→ line N capture
→ line N+1が更新済みcapture VRAMを参照可能
```

現行:

```text
192 lineの2Dをまとめて描画
→ frame末尾にcaptureをまとめてdispatch
```

同一frame feedbackを再現できない。

## 10.2 frame graph

新規候補:

```cpp
struct MetalFrameContext;
struct MetalScanlineSegment;
struct MetalCaptureOperation;
struct MetalCaptureGeneration;
```

### `MetalScanlineSegment`

```cpp
struct MetalScanlineSegment
{
    u16 StartLine;
    u16 EndLine;

    u64 FrameSerial;
    u64 RegisterGeneration;

    bool RenderEngineA;
    bool RenderEngineB;
    bool CaptureEnabled;

    MetalCaptureOperation Capture;

    std::array<u64, 16> CaptureReadGeneration;
    std::array<u64, 16> CaptureWriteGeneration;
};
```

## 10.3 segment boundary

次が変わるlineでsegmentを切る。

- CaptureCnt
- CaptureEnable
- DispCntA
- capture destination bank
- destination offset
- capture size
- source A selection
- source B selection
- EVA
- EVB
- FIFO
- source offset
- VRAM mapping
- screen swap
- master brightness
- capture-backed BG参照
- capture-backed OBJ参照
- capture-backed 3D texture参照
- 2D segment state change

## 10.4 command order

同一renderer queue:

```text
segment 0 Engine A
segment 0 Engine B
segment 0 Capture
segment 1 Engine A
segment 1 Engine B
segment 1 Capture
...
final compose
```

必要なread-after-write／write-after-read barrierをMetal feature setに応じて入れる。

## 10.5 destination generation

capture destinationへwriteするたび:

```text
layer generation++
```

subsequent segmentは新generationを読む。

previous segmentが読むtextureとcurrent segmentが書くtextureのhazardを分離する。

## 10.6 feedback方式

### 推奨A: ping-pong generation

active layerにread／write textureを用意する。

```text
read generation
write generation
swap
```

native R16Uintなのでmemory負担は小さい。

### 代替B: selective snapshot

同一layer feedback時だけsnapshotへcopy。

全layerを毎framecopyしない。

## 10.7 source A

source A 2D:

- Engine A segment outputからnative sample
- DS RGB5551へquantize
- alpha semantics再現

source A 3D:

- high-resolution targetからnative pixel centerを選ぶ
- DS capture semanticsでRGB5551へquantize
- signed X offset
- out-of-range transparent／zero behavior

## 10.8 source B

- FIFO
- plain VRAM
- canonical capture texture
- generation一致
- wrap semantics
- 128 block linear mapping
- 256 bank mapping

## 10.9 blend

integerでDS挙動を再現。

- EVA clamp 0～16
- EVB clamp 0～16
- source alpha gate
- 5-bit channel
- rounding
- saturation
- output alpha

CPU referenceとbit exactにする。

## 10.10 2D sampling

capture-backed BG／OBJはcanonical R16Uintからdecodeする。

Enhanced cacheを使う場合:

- generation一致
- missing時canonical fallback
- cacheはcorrectnessに影響しない

## 10.11 completion metadata

per-layerだけでなくper-operation tokenを持つ。

```cpp
struct CaptureWriteTicket
{
    u64 FrameSerial;
    u32 SegmentIndex;
    u32 Layer;
    u64 DirtySerial;
    u64 Generation;
    u64 Token;
};
```

old operation completionがnew generationをfinalizeしない。

## 10.12 完了条件

experimental modeで:

- all capture test diff 0
- same-frame feedback diff 0
- line途中state変更 diff 0
- Metal Raster PASS
- Metal Compute PASS
- no normal readback
- no CpuComposite candidate failure
- no RetainPrevious sustained
- memory budget内

---

# 11. PR-5: capture Full-GPU production cutover

## 11.1 前提

PR-2～PR-4のdiff 0。

## 11.2 eligibility

削除:

```cpp
if (GPU.CaptureCnt & (1u << 31))
    return false;
```

ただし、feature未対応caseを明示分類する。

無条件に除外を削除しない。

## 11.3 VBlank

capture active frameでも:

```text
Metal2D segmented render
→ per-segment capture
→ final compose
→ MetalTexture
```

を成立させる。

## 11.4 fallback policy

production:

```text
supported capture:
    Full-GPU必須

unsupported capture:
    init／feature validationで明示
    debug fallbackのみ
```

frame途中に黙ってSoftwareへ戻らない。

## 11.5 strict counter

6000 frame:

```text
captureFrames > 0
captureFullGpu == captureFrames
captureCpuFallback = 0
normalReadbackBytes = 0
retainPreviousDueCapture = 0
```

## 11.6 完了条件

M4完了。

- capture frame Full-GPU
- CPU reference diff 0
-実ROM play PASS
- homebrew PASS
- 1～16 scale PASS
- Raster／Compute PASS

---

# 12. PR-6: normal frame readback 0

## 12.1 削除対象

normal path:

- `ReadbackNativeColorTargetToLineBuffer`
- `GetLine()` CPU compositor input
- `UploadCpuCompletedCaptures`
- CPU capture scaling
- `SoftRenderer::VBlank`
- `ComposeMetalVisibleOutput`のCPU base upload
- `CpuComposite replaceRegion`

## 12.2 explicit readback API

```cpp
enum class MetalReadbackReason
{
    CpuVramRead,
    Savestate,
    Screenshot,
    VideoCapture,
    RendererSwitch,
    DebugComparison,
};
```

API:

```cpp
bool ReadbackCaptureBlock(...);
bool ReadbackFinalFrame(...);
```

normal frameから呼ばれたらstrict violation。

## 12.3 async

screenshot／video:

- staging ring
- frame serial
- completion callback
- backpressure
- frame drop policy

savestate／ARM CPU read:

- explicit wait可
- reason counter
- bytes counter
- failure handling

## 12.4 acceptance

6000 frame:

```text
normalReadbackBytes=0
explicitReadbackBytes reason別
```

---

# 13. PR-7: SoftRenderer継承撤廃

## 13.1 前提

M4／M5完了前に行わない。

## 13.2 class

現在:

```cpp
class MetalRenderer : public SoftRenderer
```

目標:

```cpp
class MetalRenderer : public Renderer
```

## 13.3 dependency map

削除対象:

```text
SoftRenderer::DrawScanline
SoftRenderer::DrawSprites
SoftRenderer::VBlank
SoftRenderer::VBlankEnd
SoftRenderer::GetFramebuffers
SoftRenderer::GetOutput
SoftRenderer::GetLine
CPU framebuffer
```

## 13.4 replacement

| Software責務 | Metal側置換 |
|---|---|
| scanline state | MetalRenderer2D segment snapshot |
| sprite state | MetalRenderer2D snapshot |
| final framebuffer | final texture array |
| 3D line | 3D texture |
| capture | Metal capture scheduler |
| brightness | MetalFrameContext |
| screen swap | MetalFrameContext |
| output | RendererOutputLease |

## 13.5 3D host interface

`MetalRenderer3D`／Compute constructorが`SoftRenderer&`を要求する。

必要責務を専用interfaceへ分離する。

```cpp
class MetalRendererHost
{
public:
    virtual ... = 0;
};
```

Software parentを偽装しない。

## 13.6 `GetFramebuffers`

Metal rendererではnormal path false。

CPU画像が必要な機能はexplicit readback APIを使う。

## 13.7 acceptance

grep:

```text
MetalRenderer : public SoftRenderer = 0
SoftRenderer:: in GPU_Metal* = 0
normal GetFramebuffers = 0
```

Software／OpenGL PASS。

---

# 14. PR-8: Metal Compute RasterReference撤廃

## 14.1 現在

```cpp
MetalRenderer3D RasterReference;
```

がmember。

`GetLine()`もRasterReferenceへ依存。

## 14.2 production policy

Metal Compute選択時:

```text
Compute output valid:
    present

Compute unsupported:
    renderer選択拒否
    UIへ理由

runtime failure:
    pause
    explicit error

Raster fallback:
    developer optionのみ
```

## 14.3 debug fallback

```text
MELONPRIME_METAL_ALLOW_DEBUG_FALLBACK=1
```

でだけ生成。

通常buildではRasterReference object自体を生成しない。

## 14.4 removal order

1. capture Compute path
2. Compute native resolve
3. explicit readback from Compute texture
4. `GetLine()`削除
5. init self-test failureをselection failureへ
6. debug comparison object分離
7. member削除
8. tooltip更新

## 14.5 acceptance

6000 frame:

```text
rasterReferenceFrames=0
GetLineCalls=0
ComputeFinalVisible=6000
```

---

# 15. PR-9: presenter MetalTexture-only

## 15.1 前提

capture Full-GPU、readback 0、SoftRenderer撤廃後。

## 15.2 `AcquireOutputLease`

削除:

```cpp
return RendererOutputLease(
    SoftRenderer::GetOutput(),
    nullptr,
    nullptr);
```

Metal renderer:

```text
MetalTexture
None
```

のみ。

## 15.3 presenter

Metal renderer選択中にCpuBgraを受けたら:

- strict violation
- displayしない
- reason log
- error policy
- stale textureへ無期限fallbackしない

## 15.4 startup grace

現在の180 frame CpuBgra graceを削除。

新policy:

```text
initializing:
    black clear
    short startup state

timeout:
    explicit error
    renderer selection failure
```

## 15.5 screenTex

CPU top／bottom upload用`screenTex`をMetal renderer pathから削除。

Software rendererをMetal presenterで表示するdebug modeは別pathへ隔離する。

## 15.6 last-known-good

許可:

- presenter backpressure
- no new drawable
-短いcommand delay

禁止:

- initialization failure隠蔽
- producer変更跨ぎ
- renderer変更跨ぎ
- sustained freeze

## 15.7 acceptance

```text
CpuBgra accepted=0
screenTex upload=0
MetalTexture presented>0
None sustained=0
```

---

# 16. PR-10: radar native Metal

## 16.1 目的

毎frameのbottom framebuffer memcpyとCPU radar合成を削除する。

## 16.2 入力

- final texture array layer 1
- HUD radar rect
- circle center
- radius
- opacity
- layout matrix
- screen swap
- rotation

## 16.3 shader

```text
layer 1 sample
→ circular mask
→ optional color transform
→ drawable blend
```

`bottomImage`不要。

## 16.4 parity

- vertical
- horizontal
- hybrid
- screen swap
- rotation
- stretch
- fullscreen
- Retina
- HUD editor

## 16.5 acceptance

```text
bottom framebuffer memcpy=0
bottomImage=0
radar CPU composite=0
```

---

# 17. PR-11: Metal HUD primitive renderer

## 17.1 command model

```cpp
enum class HudCommandType
{
    Quad,
    Line,
    Rect,
    Circle,
    Arc,
    Texture,
    GlyphRun,
    MaskedScreenSample,
};
```

```cpp
struct HudDrawCommand
{
    HudCommandType Type;
    float Z;
    float Transform[6];
    float Color[4];
    float Params[8];
    u32 TextureIndex;
    u32 FirstVertex;
    u32 VertexCount;
};
```

## 17.2 GPU resources

- dynamic vertex ring
- index buffer
- command buffer
- icon atlas
- glyph atlas
- sampler
- blend state
- scissor
- optional stencil

## 17.3 shared HUD model

既存HUD configを維持する。

Metal専用configを複製しない。

推奨:

```text
HUD scene model
├─ Software adapter
├─ OpenGL adapter
└─ Metal adapter
```

## 17.4 editor

CPU:

- hit test
- drag
- snapping
- property update

Metal:

- selected outline
- handles
- guide
- label
- preview

## 17.5 acceptance

Custom HUD ON:

```text
QPainter HUD draw=0
uiOverlay HUD bitmap=0
full UI texture upload=0
```

---

# 18. PR-12: glyph atlas／OSD／splash

## 18.1 glyph atlas

CPU生成を許可する時:

- startup
- font変更
- language変更
- missing glyph追加

毎frame生成しない。

metadata:

- glyph rect
- bearing
- advance
- baseline
- font
- language
- generation

## 18.2 OSD

OSD item:

```text
text
icon
background
lifetime
position
animation
```

として保持。

毎frameはvertex／uniform更新のみ。

## 18.3 multilingual

検証:

- Japanese
- English
- German
- Spanish
- French
- Italian
- Portuguese
- Korean
- Chinese
- Hebrew
- Arabic
- Cyrillic

missing glyphをlog。

## 18.4 splash

static texture／glyph command。

QPainter splashを削除。

## 18.5 acceptance

normal frame:

```text
QImage uiOverlay allocation=0
QPainter construction=0
UI bitmap replaceRegion=0
```

---

# 19. PR-13: macOS初回既定をMetalへ変更

## 19.1 前提

完了A acceptance通過後のみ。

## 19.2 現在

```cpp
{"3D.Renderer", renderer3D_OpenGL}
```

## 19.3 新規configだけ

macOS＋Metal build＋feature check PASS:

```text
3D.Renderer = renderer3D_Metal
Screen.UseGL = false
```

既存ユーザー設定を変更しない。

## 19.4 unsupported

- OpenGL default
- Metal option disable
- tooltip reason
- log
- crashなし

## 19.5 Compute default

別判断。

Metal Raster defaultとCompute defaultを同じcommitで変更しない。

---

# 20. PR-14: MSL asset／metallib

## 20.1 前提

画質とarchitecture固定後。

## 20.2 layout

```text
src/shaders/metal/
    Common.metal
    GPU2D.metal
    GPU3DRaster.metal
    GPU3DCompute.metal
    DisplayCapture.metal
    FinalCompose.metal
    Presenter.metal
    Hud.metal
    Osd.metal
```

## 20.3 release

```text
.metal
→ .air
→ .metallib
→ app bundle
```

release build:

```text
newLibraryWithSource=0
```

debug buildはsource compile fallback可。

## 20.4 ABI

CPU／MSL struct:

- sizeof
- alignof
- offsetof
- version

をstatic assert／testする。

---

# 21. PR-15: CI／release gate

## 21.1 build matrix

必須:

- macOS ARM64 Metal ON
- macOS Intel Metal ON
- macOS Metal OFF
- macOS FORCE_DISABLE
- Windows MinGW
- Linux

可能なら:

- Universal
- Debug
- Release
- ASan
- TSan
- UBSan

## 21.2 static forbidden-path check

完全化後にCIでgrep failure。

```text
MetalRenderer : public SoftRenderer
SoftRenderer:: in GPU_Metal
CpuBgra from MetalRenderer
RasterReference production
ReadbackNativeColorTargetToLineBuffer normal path
QPainter normal Metal frame
bottom framebuffer memcpy
newLibraryWithSource release
```

## 21.3 runtime smoke

- app launch
- Metal Raster init
- Metal Compute init
- ROM load
- 300 frame
- capture test
- scale change
- clean shutdown

## 21.4 artifact

- app bundle
- logs
- strict summary
- feature report
- perf report
- memory report
- checksum report

---

# 22. memory architecture

## 22.1 OutputState

scale 16ではfinal texture 3slotが大きい。

必要最小slot数をperformance計測で決める。

triple bufferingを根拠なく増やさない。

## 22.2 old/new overlap

state swap前にbudgetを計算する。

新state allocation failure:

```text
old state維持
config rollback
explicit error
partial publishなし
```

## 22.3 active capture cache

scale-expanded cacheはactive layerだけ。

cache miss:

```text
canonical R16Uint
→ GPU expand／decode
→ cache
```

## 22.4 device policy

8 GB unified memory:

- 16x warning／disable検討
- 8x budget check

16 GB:

- 16x conditional

32 GB以上:

- 16x許可候補

固定RAM値だけでなくMetal working set情報を使用可能範囲で確認する。

---

# 23. logging仕様

## 23.1 startup

```text
commit
device
registry ID
GPU family
recommended working set
renderer
scale
pixel format
queue model
shader version
producer ID
capture storage format
capture cache size
HUD renderer
```

## 23.2 600 frame summary

```text
frames
fullGpu
captureFrames
captureFullGpu
cpuComposite
cpuBgraPresented
normalReadbackBytes
explicitReadbackBytes
rasterReference
retainPrevious
outputNone
validationFailure
commandFailure
presentSkipped
captureMismatch
stateSwap
peakMemory
```

## 23.3 shutdown

- total frame
- strict violations
- max in-flight
- max lease
- producer transitions
- readback reason counts
- capture token peak
- memory peak
- command errors
- fallback counts

---

# 24. 受け入れ試験

## 24.1 scale

renderer:

- Metal
- Metal Compute

scale:

- 1x
- 2x
- 3x
- 4x
- 6x
- 8x
- 12x
- 16x

stress:

```text
1→2→4→8→16→8→4→2→1
```

1000 cycle。

## 24.2 renderer switch

```text
Software
→ OpenGL
→ Metal
→ Metal Compute
→ Metal
→ OpenGL
→ Software
```

100 cycle。

## 24.3 lifecycle

- ROM open
- ROM close
- reset
- soft reset
- save state
- load state
- pause
- frame step
- fast-forward
- slow motion
- fullscreen
- resize
- display移動
- Retina scale変更
- sleep
- wake
- shutdown
- multi-instance

## 24.4 ROM group

- US 1.0
- US 1.1
- EU 1.0
- EU 1.1
- JP 1.0
- JP 1.1
- KR 1.0

## 24.5 scene

- boot
- firmware
- menu
- hunter select
- adventure
- multiplayer
- map
- pause
- scan visor
- morph ball
- weapon effect
- damage flash
- cloak
- death
- respawn
- transition
- cutscene
- Display Capture heavy scene

## 24.6 strict acceptance

Metal Raster 6000 frame:

```text
cpuComposite=0
cpuBgraPresented=0
normalReadbackBytes=0
softwareFallback=0
captureCpuFallback=0
retainPrevious sustained=0
```

Metal Compute 6000 frame:

```text
rasterReference=0
cpuComposite=0
cpuBgraPresented=0
normalReadbackBytes=0
captureCpuFallback=0
```

---

# 25. performance測定

build:

```zsh
./tools/build/macos/build-macos-metal-test.sh
```

diagnostic:

```zsh
MELONPRIME_METAL_PERF=1 \
MELONPRIME_METAL_DIAG=1 \
MELONPRIME_METAL_ASSERT_GPU_ONLY=1 \
build-mac-metal/melonPrimeDS.app/Contents/MacOS/melonPrimeDS <rom>
```

strict abort:

```zsh
MELONPRIME_METAL_ASSERT_GPU_ONLY=abort \
build-mac-metal/melonPrimeDS.app/Contents/MacOS/melonPrimeDS <rom>
```

summary:

```zsh
tools/perf/summarize-melonprime-perf.py <log>
tools/perf/compare-perf-repro.py <before> <after>
```

比較条件:

- same Mac
- same build type
- same ROM
- same save
- same scene
- same duration
- same warmup
- same scale
- same VSync
- same HUD

---

# 26. 実装禁止事項

1. atomic `OutputState`をplain shared_ptr accessへ戻さない。
2. state reconfigureをin-place wait方式へ戻さない。
3. scale変更時にPresenterRefCountを強制0にしない。
4. destructorへ無期限waitを戻さない。
5. GPU writer tokenを削除しない。
6. `GpuWritesInFlight`をboolへ戻さない。
7. stale completionをValid化しない。
8. output validationをfail-openへ戻さない。
9. producer変更時にvalidation前の旧lease解放を戻さない。
10. normal frameへ`waitUntilCompleted`を追加しない。
11. global lockでrenderer／presenterを直列化しない。
12. raw texture pointerをleaseなしで非同期利用しない。
13. capture frameをCPUへ戻したまま完全Metalと呼ばない。
14. frame末尾一括captureのままM4完了としない。
15. canonical capture authorityをscale-expanded RGBA8だけにしない。
16. memory問題をone-off巨大bufferで隠さない。
17. M4完了前にSoftRenderer継承を外さない。
18. capture Compute対応前にRasterReferenceを削除しない。
19. Metal failure時に黙ってSoftwareへ戻さない。
20. Compute failure時に黙ってRasterへ戻さない。
21. CpuBgraをMetalの最終正常出力として残さない。
22. QPainter dirty-rectだけでHUD完全Metalとしない。
23. HUD configをMetal専用に複製しない。
24.画質固定前にshader asset分離を行わない。
25. acceptance前にmacOS defaultをMetalへ変えない。
26. 既存ユーザーconfigを上書きしない。
27. 共通headerへunguarded Objective-C型を追加しない。
28. Windows／Linux buildを後回しにしない。
29. 実機未検証を完了と書かない。
30. performance測定なしで高速化を主張しない。
31. diff evidenceなしでcapture parityを主張しない。
32. 1 commitへ複数phaseを混ぜない。

---

# 27. 最新版チェックリスト

## 既に実装済み、検証待ち

- [x] atomic OutputState load
- [x] atomic OutputState exchange
- [x] immutable output state swap
- [x] single-snapshot AcquireOutputLease
- [x] raw MetalTexture GetOutput撤廃
- [x] nonblocking OutputState destructor
- [x] ProducerId
- [x] Generation ordering
- [x] FrameSerial ordering
- [x] pixel format actual texture validation
- [x] sample count validation
- [x] mipmap validation
- [x] depth validation
- [x] producer validation-before-release
- [x] GPU capture in-flight count
- [x] GPU writer token
- [x] stale completion reject
- [x] pending layer index map
- [ ] macOS build evidence
- [ ] TSan evidence
- [ ] Windows／Linux evidence
- [ ] callback fault injection evidence

## output contract残件

- [ ] RendererOutput PixelFormat metadata
- [ ] automated malformed output test
- [ ] fallback reason metadata
- [ ] lease allocation optimization
- [ ] CI status

## M4

- [ ] differential scaffold
- [ ] capture homebrew test
- [ ] canonical native RGB5551 texture
- [ ] active enhanced cache
- [ ] segment scheduler
- [ ] line-precise capture
- [ ] feedback generation
- [ ] source A 2D
- [ ] source A 3D
- [ ] source B VRAM
- [ ] source B FIFO
- [ ] A+B blend
- [ ] all sizes
- [ ] all offsets
- [ ] CaptureCnt exclusion削除
- [ ] capture Full-GPU

## M5～M8

- [ ] normal readback 0
- [ ] explicit readback reason API
- [ ] SoftRenderer inheritance削除
- [ ] Software call 0
- [ ] RasterReference削除
- [ ] Compute GetLine削除
- [ ] CpuBgra Metal output削除
- [ ] screenTex CPU upload削除

## M9

- [ ] radar Metal
- [ ] HUD command list
- [ ] icon atlas
- [ ] glyph atlas
- [ ] OSD Metal
- [ ] splash Metal
- [ ] QImage normal frame 0
- [ ] QPainter normal frame 0

## M10～M12

- [ ] macOS new config Metal default
- [ ] existing config不変
- [ ] metallib
- [ ] release source compile 0
- [ ] ARM64 CI
- [ ] Intel CI
- [ ] Metal OFF
- [ ] FORCE_DISABLE
- [ ] Windows
- [ ] Linux
- [ ] release acceptance

---

# 28. 最終完了条件

## 完了A

```text
Metal Raster:
    MetalTexture-only
    Display Capture Full-GPU
    normal readback 0
    SoftRenderer dependency 0
    CpuBgra 0

Metal Compute:
    Compute final texture-only
    RasterReference 0
    Display Capture Full-GPU
    normal readback 0

Presenter:
    lease付きMetalTexture-only
    CPU screen upload 0
```

## 完了B

```text
Radar:
    Metal sample

Custom HUD:
    Metal command

OSD:
    Metal glyph／quad

Splash:
    Metal asset

Normal frame:
    QImage／QPainter pixel composition 0
```

## quality

```text
capture bit diff 0
known scene regression 0
stale frame 0
black frame 0
deadlock 0
data race 0
use-after-free 0
```

## platform

```text
macOS ARM64 PASS
macOS Intel PASS
Metal OFF PASS
FORCE_DISABLE PASS
Windows PASS
Linux PASS
```

## performance

```text
normal path regressionなし
capture path旧CPU fallback以下
HUD CPU負荷低下
memory budget内
```

---

# 29. 次のcommitへの直接指示

次の実装commitでは、M4本体をまだ変更しない。

実施:

```text
1. 最新HEADのTSan／build validation
2. output PixelFormat metadata
3. capture differential scaffoldの最小基盤
4. debug-only mismatch artifact
5. capture homebrew test設計
```

変更しない:

```text
CaptureCnt exclusion
SoftRenderer inheritance
RasterReference
presenter CpuBgra policy
HUD
default renderer
embedded shader asset
```

そのcommitを受け入れた後、native canonical capture storageへ進む。

---

# 30. 最終指示

各PRで提出するもの:

```text
1. 変更ファイル
2. 根本原因
3. ownership contract
4. ordering contract
5. fallback残存
6. strict counter
7. build matrix
8.実ROM検証
9. test ROM検証
10. pixel diff
11. performance before／after
12. memory before／after
13. 未完了事項
```

完全Metal化は、Metal APIを呼んでいるかではなく、通常フレームの可視pixelが通った経路で判定する。

> 最終正規経路は、Metal 3D Raster／Compute → line-precise Metal Display Capture → segmented Metal 2D → Metal final texture array → Metal HUD／OSD → CAMetalLayerであり、通常フレームにCPU pixel round-trip、Software compositor、CpuBgra presentation、RasterReference fallbackが存在しない状態とする。
