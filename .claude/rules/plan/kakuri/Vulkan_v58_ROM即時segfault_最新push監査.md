# Vulkan ROM起動直後 Segmentation Fault
## v58 最新push監査・修正指示書

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査時HEAD:** `9a283eed4387670d956ef6a8870ed3fd7394f03f`  
**確認build:** `melonPrimeDS v3.4.3 v58.exe`  
**症状:** VulkanでROMを起動すると最初の`RunFrame()`直後にSegmentation Fault。Software／OpenGL等では発生しない。

---

# 0. 結論

今回の即落ち原因は、`src/GPU_Soft.cpp`のVulkan用packed framebuffer書き込みにある。

`SoftRenderer::DrawScanline()`は、すでに対象scanlineの先頭へ進めた`dstA`／`dstB`を
`WriteAcceleratedPackedRow()`へ渡している。

しかし`WriteAcceleratedPackedRow()`側でも、もう一度:

```cpp
const size_t rowBase = static_cast<size_t>(line) * kPackedStride;
```

を加算している。

実効書き込み位置は:

```text
Framebuffer base
+ line * stride       // DrawScanline側
+ line * stride       // WriteAcceleratedPackedRow側
```

となり、実際には`2 * line`行目へ書いている。

Vulkan packed framebufferは192行しかないため:

```text
line 0～95   : allocation内
line 96      : allocation終端の直後から書き込み
line 96～191 : heap外書き込み
```

になる。

これはVulkan時だけ有効になる経路であり、v58ログの:

```text
first RunFrame begin
completeProducerTransaction result=0
first RunFrame complete lines=263
first drawScreen begin
first drawScreen complete
Segmentation fault
```

と完全に一致する。

最初のframe中にheapを破壊し、`RunFrame()`から戻った後、
Qt callback、allocator、Vulkan session、または次の処理で破壊済みmemoryへ触れて落ちている。

**presenter、shader、timeline semaphore、JITを先に修正してはいけない。**

---

# 1. v58ログから確定した実行位置

Vulkan初期化は成功している。

```text
VulkanContext ready
Vulkan Renderer3D initialized
VulkanOutput: sync path initialized
frontend session initialize complete
```

ROMの読込、NDS reset、cart install、`nds->Start()`も成功している。

```text
Game is now booting
msg_EmuRun complete
```

最初のemulation frameも最後まで走っている。

```text
first RunFrame begin
first RunFrame complete lines=263
```

しかしproducer完了は失敗している。

```text
completeProducerTransaction result=0
```

その後、`EmuInstance::drawScreen()`からも一旦戻っている。

```text
first drawScreen begin
first drawScreen complete
```

直後にhost processが:

```text
Segmentation fault
```

で終了する。

この並びは、guest ARM9 exceptionよりも先に
host heap corruptionを疑うべき形である。

---

# 2. Critical: packed framebufferの二重line offset

対象:

```text
src/GPU_Soft.cpp
SoftRenderer::DrawScanline()
SoftRenderer::WriteAcceleratedPackedRow()
```

## 2.1 呼出側

現在の`DrawScanline()`は:

```cpp
const u32 stride = accelerated
    ? static_cast<u32>(kPackedStride)
    : 256u;

u32 dstoffset = stride * line;

if (GPU.ScreenSwap)
{
    dstA = &Framebuffer[BackBuffer][0][dstoffset];
    dstB = &Framebuffer[BackBuffer][1][dstoffset];
}
else
{
    dstA = &Framebuffer[BackBuffer][1][dstoffset];
    dstB = &Framebuffer[BackBuffer][0][dstoffset];
}
```

としている。

この時点で`dstA`／`dstB`は:

```text
対象bufferの先頭
＋
line * kPackedStride
```

であり、対象行の先頭を指している。

その後:

```cpp
WriteAcceleratedPackedRow(
    engine == 0 ? dstA : dstB,
    engine,
    line,
    ...);
```

と渡している。

## 2.2 被呼出側

現在の`WriteAcceleratedPackedRow()`は:

```cpp
const size_t rowBase =
    static_cast<size_t>(line) * kPackedStride;

std::memcpy(
    dst + rowBase,
    StructuredPlane0[engine],
    kStructuredScreenWidth * sizeof(u32));
```

としている。

したがって、行offsetが二重に加算される。

## 2.3 allocationと破壊量

定数:

```text
kPackedStride = 256 * 3 + 1 = 769 u32
height = 192
```

1画面buffer:

```text
769 * 192 = 147,648 u32
147,648 * 4 = 590,592 bytes
```

`line == 96`:

```text
DrawScanline側offset = 96 * 769
関数内offset         = 96 * 769
合計                 = 192 * 769
```

これはallocationの正確なone-past-endである。

`line == 191`の最後まで書くと、1画面bufferあたり最大:

```text
146,879 u32
587,516 bytes
```

をallocation外へ書き得る。

上画面／下画面の両方で実行されるため、
周辺heap object、別framebuffer、allocator metadata、
Vulkan関連objectのmemoryを広範囲に破壊できる。

---

# 3. 最優先修正 V58-1

呼出側がrow pointerを渡す設計を維持し、
`WriteAcceleratedPackedRow()`内の`rowBase`を削除する。

## 修正例

```cpp
void SoftRenderer::WriteAcceleratedPackedRow(
    u32* dstRow,
    u32 engine,
    u32 line,
    u16 masterBrightness,
    u32 dispCnt,
    bool forcedBlank,
    bool engineEnabled)
{
    if (dstRow == nullptr
        || engine >= kStructuredScreenCount
        || line >= kStructuredScreenHeight)
    {
        return;
    }

    std::memcpy(
        dstRow,
        StructuredPlane0[engine],
        kStructuredScreenWidth * sizeof(u32));

    std::memcpy(
        dstRow + kStructuredScreenWidth,
        StructuredPlane1[engine],
        kStructuredScreenWidth * sizeof(u32));

    std::memcpy(
        dstRow + (kStructuredScreenWidth * 2u),
        StructuredControl[engine],
        kStructuredScreenWidth * sizeof(u32));

    const u32 dispmode =
        (dispCnt >> 16u) & (engine == 0 ? 0x3u : 0x1u);

    u32 meta =
        static_cast<u32>(masterBrightness)
        | (dispCnt & 0x30000u)
        | (dispmode << 16u);

    const u32 xpos =
        static_cast<u32>(GPU.GPU3D.GetRenderXPos()) & 0x1FFu;

    meta |=
        (xpos << 24u)
        | ((xpos & 0x100u) << 15u);

    if (forcedBlank || !engineEnabled || !GPU.ScreensEnabled)
        meta = 0u;

    dstRow[kPackedStride - 1u] = meta;
}
```

重要点:

```text
dstRow + line * stride
```

を再計算しない。

`line`引数は:

```text
範囲検証
metadata
diagnostic
```

のみに使用する。

## 代替修正

buffer baseを渡す設計に統一する方法もある。

ただし現在の呼出側はscreen swapを反映したrow pointerを既に構築しているため、
今回の最小・安全な修正は被呼出側から二重offsetを除去する方法である。

---

# 4. High: FrontBufferとFramebuffer indexの対応が壊れている

対象:

```text
src/GPU_Soft.cpp
SoftRenderer::SyncSapphireFramebufferBindings()
```

現在:

```cpp
const int frontbuf = BackBuffer ^ 1;
GPU.FrontBuffer = frontbuf;

GPU.Framebuffer[0][0] = Framebuffer[frontbuf][0];
GPU.Framebuffer[0][1] = Framebuffer[frontbuf][1];

GPU.Framebuffer[1][0] = Framebuffer[frontbuf ^ 1][0];
GPU.Framebuffer[1][1] = Framebuffer[frontbuf ^ 1][1];
```

これは二つの異なる方式を混ぜている。

```text
GPU.FrontBuffer:
physical buffer indexとして公開

GPU.Framebuffer[0]:
logical front bufferとして再配置

GPU.Framebuffer[1]:
logical back bufferとして再配置
```

一方、Sapphire latchは:

```cpp
topPackedRaw = GPU.Framebuffer[GPU.FrontBuffer][0];
bottomPackedRaw = GPU.Framebuffer[GPU.FrontBuffer][1];
```

と読む。

例:

```text
frontbuf = 1

GPU.FrontBuffer = 1
GPU.Framebuffer[0] = physical Framebuffer[1]  // actual front
GPU.Framebuffer[1] = physical Framebuffer[0]  // actual back
```

latchはindex 1を読むため、actual back bufferを読む。

最初のframeでは偶然一致しても、
buffer swapの次frameから逆側を読む。

これは即時segfault修正後に:

```text
1frame遅延
交互に古いframe
白画面
screen swap不整合
capture ownership不整合
```

を起こす。

---

# 5. 修正 V58-2

`GPU.Framebuffer[index]`をphysical indexのまま公開し、
`GPU.FrontBuffer`だけを現在のfront indexとして更新する。

```cpp
void SoftRenderer::SyncSapphireFramebufferBindings() noexcept
{
    GPU.FrontBuffer = BackBuffer ^ 1;

    GPU.Framebuffer[0][0] = Framebuffer[0][0];
    GPU.Framebuffer[0][1] = Framebuffer[0][1];

    GPU.Framebuffer[1][0] = Framebuffer[1][0];
    GPU.Framebuffer[1][1] = Framebuffer[1][1];
}
```

これにより:

```cpp
GPU.Framebuffer[GPU.FrontBuffer][screen]
```

が常にactual front bufferを返す。

## 必須assert

debug buildで:

```cpp
assert(GPU.FrontBuffer == 0 || GPU.FrontBuffer == 1);
assert(GPU.Framebuffer[GPU.FrontBuffer][0]
    == Framebuffer[BackBuffer ^ 1][0]);
assert(GPU.Framebuffer[GPU.FrontBuffer][1]
    == Framebuffer[BackBuffer ^ 1][1]);
```

を追加する。

---

# 6. High: nullable objectをreferenceで無条件dereferenceしている

対象:

```text
src/GPU.cpp
GPU::GetSapphireRenderer2D()
```

現在:

```cpp
return *SapphireVulkan2DAccess;
```

`SapphireVulkan2DAccess`は、outer rendererが`SoftRenderer`の場合のみ生成される。

```cpp
if (auto* softRenderer = dynamic_cast<SoftRenderer*>(Rend.get()))
{
    SapphireVulkan2DAccess = std::make_unique<...>();
}
else
{
    SapphireVulkan2DAccess.reset();
}
```

backend switch、fallback、初期化失敗、将来のouter renderer変更時に
null dereferenceになり得る。

## 修正 V58-3

pointer APIを追加する。

```cpp
[[nodiscard]]
SapphireGPU2D::SoftRenderer*
TryGetSapphireRenderer2D() noexcept
{
    return SapphireVulkan2DAccess.get();
}

[[nodiscard]]
const SapphireGPU2D::SoftRenderer*
TryGetSapphireRenderer2D() const noexcept
{
    return SapphireVulkan2DAccess.get();
}
```

latch側:

```cpp
const auto* renderer2D =
    useStructuredVulkan2D
        ? nds_->GPU.TryGetSapphireRenderer2D()
        : nullptr;

if (useStructuredVulkan2D && renderer2D == nullptr)
    return false;
```

同様にresync処理もnull checkする。

---

# 7. Medium: facadeはSapphire完全準拠になっていない

対象:

```text
src/SapphireGPU2DSoftAccess.h
```

現在:

```cpp
const u32* GetDebugCapture3dSource() const noexcept
{
    return nullptr;
}

const std::array<u8, 192>&
GetDebugCaptureLineUses3dMask() const noexcept
{
    static const std::array<u8, 192> empty{};
    return empty;
}
```

つまりSapphire latchを移植していても:

```text
capture 3D source
capture line ownership mask
partial capture判定
temporal capture fallback
```

は常に欠落する。

これは今回の即時segfault原因ではないが、
heap overflow修正後の表示／capture不具合につながる。

「Sapphire exact port」とはまだ呼べない。

## 対応 V58-4

Sapphire core commit:

```text
d77944275fa61f9b79cfcead2c3e98993429a023
```

の対応実装から、次を実データとして移植する。

```text
capture 3D source buffer
capture line uses-3D mask
capture statistics更新
regular capture metadata
VRAM capture metadata
```

未実装時は「空を返して処理継続」ではなく、
機能capabilityを明示してfallbackを選択する。

---

# 8. `completeProducerTransaction result=0`の扱い

現在のlogは最終結果しか出していないため、
次のどこで失敗したか不明。

```text
Vulkan3DFrameView.Valid == false
FrameSerial == 0
FrameSerial == lastSubmittedSerial
Generation mismatch
latchSoftPackedFrameSnapshot() failure
regular capture resync
ensureFrameResources() failure
prepareFrameForPresentation() failure
```

ただし、今回のheap overflowは`RunFrame()`内で既に発生しているため、
このresult 0自体も破壊済みmemoryの影響を受け得る。

**まずoverflowを修正し、再ログを取る。**

## 修正 V58-5: stage-specific trace

`completeProducerFrame()`へ以下を追加する。

```cpp
Platform::Log(
    Platform::LogLevel::Info,
    "[VulkanProducer] frame=%p valid=%d serial=%llu "
    "generation=%llu activeGeneration=%llu "
    "frontBuffer=%d screenSwap=%d\n",
    static_cast<void*>(frame),
    frameView.Valid ? 1 : 0,
    static_cast<unsigned long long>(frameView.FrameSerial),
    static_cast<unsigned long long>(frameView.Generation),
    static_cast<unsigned long long>(activeGeneration),
    nds ? nds->GPU.FrontBuffer : -1,
    nds ? (nds->GPU.GPU3D.RenderScreenSwapAt3D ? 1 : 0) : -1);
```

失敗returnごとにreasonを付ける。

```text
invalidFrameView
zeroSerial
duplicateSerial
generationMismatch
latchFailed
resyncRequested
ensureResourcesFailed
prepareFailed
```

例:

```cpp
[VulkanProducer] discard reason=latchFailed
```

---

# 9. GUI callbackは一次原因ではない

`ScreenPanelVulkan::drawScreen()`はGUI callbackをqueueするが、
v58では`drawScreen complete`後にsegfaultしている。

一見するとqueued callbackが疑わしい。

しかし今回、`RunFrame()`中の確定的なheap外書き込みが存在する。

heap corruptionでは:

```text
破壊した場所
```

と:

```text
実際に落ちる場所
```

は一致しない。

したがって、現時点で:

```text
presentOnGuiThread()
NoRomSplashOverlay
Qt invokeMethod
Vulkan swapchain
```

を一次原因と判断しない。

overflow修正後も落ちる場合にのみ、
以下へtraceを追加する。

```text
drawScreen exit
DeferredDrain begin／end
presentOnGuiThread entry
ensureNativeSurface begin／end
configureSurface begin／end
buildOverlay begin／end
acquirePresentFrame begin／end
```

---

# 10. 修正順序

## Commit 1 — 必須

```text
Fix double scanline offset in Vulkan packed framebuffer writes
```

変更:

```text
WriteAcceleratedPackedRow()のrowBase削除
dstをdstRowへrename
engine range check追加
```

## Commit 2 — 必須

```text
Fix physical FrontBuffer bindings for the Sapphire latch
```

変更:

```text
GPU.Framebuffer[0／1]をphysical indexで公開
GPU.FrontBufferのみcurrent frontへ更新
debug assert追加
```

## Commit 3 — 安全性

```text
Make Sapphire GPU2D access nullable during backend transitions
```

変更:

```text
TryGetSapphireRenderer2D()
latch／resync null check
```

## Commit 4 — 診断

```text
Log exact Vulkan producer discard reasons
```

## Commit 5 — parity

```text
Port Sapphire capture source and line ownership metadata
```

Commit 1と2を同じcommitにまとめてもよいが、
presenter、shader、FrameQueue変更を混ぜない。

---

# 11. 回帰テスト

# T1: AddressSanitizer

Vulkan buildへ:

```text
-fsanitize=address,undefined
-fno-omit-frame-pointer
```

を付ける。

期待:

```text
first RunFrameでheap-buffer-overflowなし
```

## T2: boundary canary

debug buildのみ、各packed framebufferの後ろへguard領域を置く。

```text
64～256 u32
0xDEADC0DE
```

各frame完了時に全guardを確認する。

## T3: row sentinel

frame開始前に各row metadata slot:

```text
row * 769 + 768
```

を固有値で初期化し、
frame終了後に192行だけ更新されていることを確認する。

## T4: FrontBuffer alternation

3frame以上:

```text
BackBuffer
GPU.FrontBuffer
physical pointer
frame serial
```

を出す。

期待例:

```text
BackBuffer=1 FrontBuffer=0 pointer=Framebuffer[0]
BackBuffer=0 FrontBuffer=1 pointer=Framebuffer[1]
BackBuffer=1 FrontBuffer=0 pointer=Framebuffer[0]
```

## T5: first producer

overflow修正後:

```text
completeProducerTransaction result
failure reason
```

を確認する。

first frameが`zeroSerial`等でskipされること自体は許容できる。

ただし、2～3frame以内に:

```text
prepare
queue push
acquire
present
commit
```

へ進むこと。

## T6: non-Vulkan regression

```text
Software
OpenGL
OpenGL Compute
```

で同じROMを起動する。

今回の修正はVulkan compile gate内に限定する。

## T7: long run

```text
10分
ROM stop／restart 20回
Software→Vulkan→Software 20回
fullscreen／resize
1x／2x／4x
```

を実施する。

---

# 12. 禁止事項

今回の一次修正では行わない。

```text
FrameQueue slot数変更
present timeout変更
timeline semaphore無効化
shader書換え
JIT無効化を恒久対策にする
Qt callbackを削除
CPU framebuffer fallback追加
result=0を無条件trueへ変更
```

これらはheap overflowを隠すだけで、
memory corruptionを解消しない。

---

# 13. 完了条件

```text
WriteAcceleratedPackedRowがrow offsetを一度だけ適用
line 96～191でallocation外writeなし
FrontBuffer indexとphysical pointerが一致
ASan／UBSan clean
first RunFrame後にsegfaultしない
producer failure reasonが特定可能
2～3frame以内にqueue push／present／commit
Software／OpenGLに回帰なし
backend switch時にSapphire access null dereferenceなし
```

---

# 14. 最終判断

今回のクラッシュは「Vulkan presenterの不安定さ」ではない。

```text
Vulkan用packed framebufferのCPU書き込みで、
scanline offsetを二重加算したことによる確定的なheap overflow
```

である。

Vulkan時だけ:

```cpp
accelerated == true
```

となり`WriteAcceleratedPackedRow()`を通るため、

```text
Vulkanだけ即落ち
Vulkan以外は正常
```

という症状とも一致する。

最初に`GPU_Soft.cpp`の二重offsetを修正し、
続いてFrontBuffer bindingを修正すること。
