# Vulkan S78 監査・フェーズ別修正指示書
## First RunFrame / Unit B ACCESS_VIOLATION
## Sapphire GPU2D Event Lifecycle 完全移植
## 特殊レジスタ単一所有化
## Android／Desktop差分の限定

**作成日:** 2026-07-16  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `edbdda85434ba3c1d9790cfc5ed15445cc3f1a76`  
**前回監査HEAD:** `399a5014d1c0a50b4ee79de3a019d79cc8cca925`  
**S77-11:** `145e466b13d634add71cdaefff32b10f5509a550`  
**S77-13:** `ad4cf2965`  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 1. 監査結論

S77-11とS77-13は正しい方向へ進んでいる。

完了済みの有効な変更:

```text
- GPUがGPU2D framebufferを所有
- Sapphire GPU2D Unit A/BをGPUが所有
- Sapphire GPU2D Renderer2DをGPUが所有
- UnitSyncと旧adapterを削除
- SoftRenderer::SwapBuffers型のbuffer lifecycleへ復元
- 実プロセスでROMを起動するcold-start testを追加
- Windows ACCESS_VIOLATIONを明示的に失敗として検出
- Unit A scanline 0完了後、Unit B DrawScanline内で落ちることを確認
```

ただし、現在の実装はまだ固定Sapphireと同一ではない。

現在は次の混成状態になっている。

```text
Sapphire GPU2D Unit A/B
Sapphire GPU2D SoftRenderer
GPU-owned framebuffer
        +
melonDS Desktop GPU event lifecycle
melonDS Desktop outer Renderer lifecycle
melonDS Desktop特殊2Dレジスタ所有
独自Vulkan activation/publication gate
```

特に次の関数群が固定Sapphireの実装・呼出し順序になっていない。

```text
GPU::StartFrame
GPU::StartHBlank
GPU::StartScanline
GPU::FinishFrame
GPU::BlankFrame
GPU::SetPowerCnt
GPU::DisplayFIFO
GPU特殊2DレジスタRead/Write
```

UnitだけSapphireへ差し替え、GPUイベント順序を旧Desktopのまま残す構成は、
Sapphire完全移植ではない。

今回の本命修正はUnit Bだけの例外処理ではなく、

```text
固定Sapphire GPU.cppのGPU2D dependency closureを
呼出し順序ごと移植する
```

ことである。

---

# 2. 現在のクラッシュ位置

実行ログ:

```text
[FirstGpu2D] before UnitB line=0
[MelonPrimeCrash] ...
exit code 0xC0000005
```

確認済み:

```text
- Sapphire Vulkan activation完了
- first Vulkan producer begin到達
- first RunFrame begin到達
- framebuffer binding成功
- Unit A DrawScanline(0)完了
- Unit B DrawScanline(0)内部でACCESS_VIOLATION
```

Unit Bの`DrawScanline()`内部は概ね次の順序。

```text
1. CurUnit = UnitB
2. _3DLine初期化
3. framebuffer stride決定
4. Unit B framebuffer row pointer算出
5. physical screen判定
6. structured metadata line clear
7. BBG dirty state導出
8. BBG flat VRAM coherence
9. BBG extended palette coherence
10. BOBJ extended palette coherence
11. forceblank判定
12. BG/OBJ scanline描画
13. mosaic counter更新
14. display mode出力
15. master brightness
16. packed framebuffer変換
```

現在のtraceでは、このどの処理でfaultしたかは未確定。

したがって、

```text
BBG coherenceが原因
BOBJが原因
framebufferが原因
未初期化sprite stateが原因
```

のいずれかを、stackなしで断定してはならない。

---

# 3. 確定している構造上の問題

## 3.1 GPU2D event lifecycleが固定Sapphireではない

現在のVulkan pathは、GPUからouter `Renderer`へ入り、
その中からSapphire Renderer2Dを呼んでいる。

```cpp
Rend->DrawScanline(line);
Rend->DrawSprites(line + 1);
```

outer `SoftRenderer`内部:

```cpp
renderer2D.DrawScanline(line, &GPU.GPU2D_A);
renderer2D.DrawScanline(line, &GPU.GPU2D_B);
```

固定SapphireはGPUから直接呼ぶ。

```cpp
GPU2D_Renderer->DrawScanline(line, &GPU2D_A);
GPU2D_Renderer->DrawScanline(line, &GPU2D_B);

GPU2D_Renderer->DrawSprites(line + 1, &GPU2D_A);
GPU2D_Renderer->DrawSprites(line + 1, &GPU2D_B);
```

現在は呼出し階層・binding timing・VBlank順序が固定Sapphireと異なる。

---

## 3.2 特殊2Dレジスタが二重所有

Sapphire Unitが所有する状態:

```text
DispFIFO[16]
DispFIFOReadPtr
DispFIFOWritePtr
DispFIFOBuffer[256]
CaptureLatch
CaptureCnt
MasterBrightness
```

Desktop GPUにも残っている状態:

```text
DispFIFO[16]
DispFIFOReadPtr
DispFIFOWritePtr
DispFIFOBuffer[256]
CaptureCnt
CaptureEnable
MasterBrightnessA
MasterBrightnessB
```

現在のregister handlerはDesktop GPU側へ書き込む一方、
Sapphire rendererは`CurUnit`側を読む。

つまり、

```text
register write先 != renderer read元
```

になり得る。

UnitSyncファイルが削除されても、
state ownership自体はまだ一本化されていない。

---

## 3.3 ScreenSwapとframebuffer bindingの更新順序が異なる

現在:

```cpp
ScreenSwap = !!(val & (1 << 15));
```

だけで、`SetPowerCnt()`直後に`AssignFramebuffers()`を呼ばない。

固定Sapphire:

```cpp
if (NDS.PowerControl9 & (1 << 15))
    GPU2D_Renderer->SetFramebuffer(slot0, slot1);
else
    GPU2D_Renderer->SetFramebuffer(slot1, slot0);
```

さらに`SetPowerCnt()`の最後で即座にbindingを更新する。

判断元は`ScreenSwap` shadowではなく、
`NDS.PowerControl9 bit15`へ統一すべき。

---

## 3.4 Reset時のframebuffer内容と順序が異なる

固定SapphireはReset時に全framebufferを`0xFFFFFFFF`で初期化する。

現在のMelonPrime Vulkan pathは外側rendererからゼロclearしている。

これはSEGVの直接原因とは限らないが、

```text
cold-start初期画面
forceblank
first publication
splash hide
white frame判定
```

へ影響する。

---

# 4. 修正の基本原則

## 4.1 そのまま移植する範囲

次のファイル・関数群は、
可能な限り固定Sapphireからそのまま持ってくる。

```text
GPU2D.h
GPU2D.cpp
GPU2D_Soft.h
GPU2D_Soft.cpp
```

GPU側:

```text
GPU constructor
GPU::Reset
GPU::DoSavestate
GPU::AssignFramebuffers
GPU::InitFramebuffers
GPU::SetRenderer3D
GPU::SetPowerCnt
GPU::DisplayFIFO
GPU::StartFrame
GPU::StartHBlank
GPU::StartScanline
GPU::FinishFrame
GPU::BlankFrame
GPU::SetDispStat
GPU::SetVCount
VRAMTrackingSet::DeriveState
MakeVRAMFlat_*Coherent
```

許容する差:

```text
- namespace
- include path
- build gate
- logging
- minidump
- Desktop publication callback
- Vulkan surface/resource lifetime adapter
```

許容しない差:

```text
- Unit A/B呼出し順序
- DrawScanline/DrawSprites順序
- VBlank/VBlankEnd順序
- framebuffer slot意味
- screen swap source
- FIFO ownership
- display capture ownership
- brightness ownership
- mosaic/window timing
```

---

# 5. フェーズ別修正指示

---

# Phase 0 — 証拠固定と再現条件の確定

## 目的

現在のSEGVを、修正前の基準として再現可能な状態に固定する。

## 対象

```text
tools/test_sapphire_vulkan_cold_start_s77.py
tools/fixtures/synthetic_vulkan_cold_start.nds
MelonPrimeFirstVulkanFrameTrace.*
MelonPrimeWindowsCrashHandler.*
.github/workflows/sapphire-vendor-parity.yml
```

## 作業

1. 現在のcold-start fixtureを変更せず保存する。
2. 実行binaryのcommit SHAをログへ出す。
3. binary SHA-256をログへ出す。
4. config内容をログへ出す。
5. ROM SHA-256をログへ出す。
6. minidumpファイル名をtest outputへ出す。
7. crash exit codeを必ず非0失敗として扱う。
8. stdout/stderr末尾だけでなく完全ログをCI artifactへ保存する。

## 禁止事項

```text
- crash時にexit code 0へ変換
- checkpoint不足をwarningだけにする
- fixtureを途中で別ROMへ差し替える
- Releaseだけで再現する状態を放置
```

## 完了条件

```text
同一commit、同一ROM、同一configで
複数回Unit B line 0 SEGVが再現する
```

## 推奨コミット

```text
S78-1: Freeze Unit-B cold-start crash reproduction artifacts
```

---

# Phase 1 — Unit B内部のfault区間を特定

## 目的

`before UnitB`からfault命令までの範囲を段階的に縮小する。

## 対象

```text
src/SapphireGPU2DCore/GPU2D_Soft.cpp
src/MelonPrimeFirstVulkanFrameTrace.h
```

## 作業

Unit B、line 0、first RunFrame限定で次のtraceを追加。

```text
[UnitB0] enter
[UnitB0] framebuffer begin/end
[UnitB0] screenTarget begin/end
[UnitB0] clearStructured begin/end
[UnitB0] deriveBBG begin/end
[UnitB0] coherentBBG begin/end
[UnitB0] deriveBBGExt begin/end
[UnitB0] coherentBBGExt begin/end
[UnitB0] deriveBOBJExt begin/end
[UnitB0] coherentBOBJExt begin/end
[UnitB0] forceblank
[UnitB0] bgobj begin/end
[UnitB0] mosaic begin/end
[UnitB0] display begin/end
[UnitB0] brightness begin/end
[UnitB0] complete
```

各stageで記録する値:

```text
CurUnit
CurUnit->Num
CurUnit->Enabled
CurUnit->DispCnt
CurUnit->BGCnt[0..3]
Framebuffer[0]
Framebuffer[1]
dst
stride
GPU.VCount
NDS.PowerControl9
ScreenSwap shadow
VRAMMap_BBG[0..7]
VRAMMap_BBGExtPal[0..3]
VRAMMap_BOBJExtPal
```

## 実装条件

```text
- first cold-start test時だけ有効
- production通常実行では無効
- 各ログ出力後にflush
- pointer dereference前にaddressを記録
```

## 完了条件

次のいずれかまで特定。

```text
- faultする関数
- faultするloop
- faultするpointer
- faultする配列index
```

## 推奨コミット

```text
S78-2: Add first-frame Unit-B sub-stage crash trace
```

---

# Phase 2 — シンボル付きstackとsanitizer結果を取得

## 目的

未初期化・範囲外・use-after-free・null参照を分類する。

## 対象

```text
.claude/skills/build-mingw-vulkan-debug.bat
CMake sanitizer options
.github/workflows/sapphire-vendor-parity.yml
MelonPrimeWindowsCrashHandler.*
```

## Windows Debug

推奨flags:

```text
-g3
-O0
-fno-omit-frame-pointer
```

可能であれば:

```text
-gcodeview
```

取得する情報:

```text
exception address
module base
faulting thread
registers
symbolized stack
source file
source line
instruction
```

## Linux ASan/UBSan

推奨flags:

```text
-fsanitize=address,undefined
-fno-omit-frame-pointer
-O1
-g3
```

## MemorySanitizer

未初期化値が疑われるため、
可能なら専用jobで実行。

```text
-fsanitize=memory
-fsanitize-memory-track-origins=2
```

MSanは依存libraryもinstrumentする必要があるため、
通常build jobとは分離する。

## 完了条件

faultを次の分類へ落とす。

```text
A. null pointer
B. heap-buffer-overflow
C. stack-buffer-overflow
D. use-after-free
E. uninitialized value
F. invalid object lifetime
G. alignment/aliasing
H. その他
```

## 推奨コミット

```text
S78-3: Add symbolized Windows Debug cold-start crash job
S78-4: Add Linux ASan and UBSan Vulkan cold-start jobs
S78-5: Add optional MemorySanitizer GPU2D cold-start job
```

---

# Phase 3 — Sapphire GPU2D event lifecycleをそのまま移植

## 目的

Sapphire Unitを旧Desktop event engineから切り離す。

## 対象

```text
src/GPU.cpp
src/GPU.h
src/GPU_Soft.cpp
src/GPU_Soft.h
src/SapphireVendor/upstream/melonDS-android-lib/src/GPU.cpp
```

## 移植対象

固定Sapphireから以下を直接移植。

```text
GPU::StartFrame
GPU::StartHBlank
GPU::StartScanline
GPU::FinishFrame
GPU::BlankFrame
```

## 必須構造

```cpp
GPU2D_Renderer->DrawScanline(line, &GPU2D_A);
GPU2D_Renderer->DrawScanline(line, &GPU2D_B);

GPU2D_Renderer->DrawSprites(line + 1, &GPU2D_A);
GPU2D_Renderer->DrawSprites(line + 1, &GPU2D_B);
```

## 削除対象

Vulkan canonical pathにおける:

```text
Rend->DrawScanlineからSapphire Unitを呼ぶbridge
Rend->DrawSpritesからSapphire Unitを呼ぶbridge
毎scanline GPU.AssignFramebuffers()
outer RendererによるVBlankEnd bridge
```

## Desktop固有処理の配置

GPU2D描画完了後に必要なDesktop publicationだけを
明示hookとして追加。

例:

```cpp
FinishFrame()
{
    // Sapphire exact lifecycle
    FrontBuffer ^= 1;
    AssignFramebuffers();

    // Desktop-only adapter
    PublishCompletedDesktopFrame();
}
```

publication hookはGPU2Dの呼出し順序を変更しない。

## 完了条件

```text
GPU::StartHBlankからSapphire Renderer2Dを直接呼ぶ
outer Rendererを経由しない
Unit A→Unit B順序が固定Sapphireと一致
DrawSprites A→B順序が固定Sapphireと一致
```

## 推奨コミット

```text
S78-6: Port pinned Sapphire GPU2D event lifecycle verbatim
```

---

# Phase 4 — 特殊2Dレジスタをcanonical Unitへ一本化

## 目的

register write先とrenderer read元を一致させる。

## 対象

```text
src/GPU.h
src/GPU.cpp
src/SapphireGPU2DCore/Unit.h
src/SapphireGPU2DCore/Unit.cpp
src/NDS.cpp
```

## canonical ownership

Unit A:

```text
DispFIFO
DispFIFOReadPtr
DispFIFOWritePtr
DispFIFOBuffer
CaptureCnt
CaptureLatch
MasterBrightness
```

Unit B:

```text
MasterBrightness
```

## 削除または非canonical化するGPU側state

```text
GPU::DispFIFO
GPU::DispFIFOReadPtr
GPU::DispFIFOWritePtr
GPU::DispFIFOBuffer
GPU::CaptureCnt
GPU::CaptureEnable
GPU::MasterBrightnessA
GPU::MasterBrightnessB
```

他renderer互換のため残す場合も、
canonical判断元にしてはならない。

## register routing

次を固定Sapphire Unitへ直接routing。

```text
0x04000064-0x04000067 → Unit A CaptureCnt
0x04000068-0x0400006B → Unit A FIFO
0x0400006C-0x0400006D → Unit A MasterBrightness
0x0400106C-0x0400106D → Unit B MasterBrightness
```

## savestate

save/load対象もUnit所有へ一本化。

同じ値をGPUとUnitの双方へ保存しない。

## 完了条件

```text
CaptureCnt owner = Unit Aのみ
FIFO owner = Unit Aのみ
MasterBrightness owner =各Unit
register read/write先 = renderer read元
同期adapter = 0
```

## 推奨コミット

```text
S78-7: Move FIFO capture and brightness registers into canonical Sapphire Units
```

---

# Phase 5 — POWCNT・ScreenSwap・framebuffer bindingをSapphire化

## 目的

物理画面割当てを1つのsourceと1つのtimingへ統一する。

## 対象

```text
src/GPU.cpp
src/GPU.h
src/GPU_Soft.cpp
```

## 作業

固定Sapphireの`SetPowerCnt()`と`AssignFramebuffers()`を移植。

判断元:

```text
NDS.PowerControl9 bit15
```

binding:

```cpp
if (NDS.PowerControl9 & (1 << 15))
    GPU2D_Renderer->SetFramebuffer(slot0, slot1);
else
    GPU2D_Renderer->SetFramebuffer(slot1, slot0);
```

`SetPowerCnt()`の最後で必ず:

```cpp
AssignFramebuffers();
```

を呼ぶ。

## ScreenSwap shadow

他コードの互換用に残す場合:

```text
read-only derived cache
```

とする。

次の判断には使用しない。

```text
framebuffer assignment
capture screen identity
physical top/bottom判定
3D screen ownership
```

## 完了条件

```text
screen swap canonical source = PowerControl9 bit15
SetPowerCnt直後にbinding更新
framebuffer slot意味が固定Sapphireと一致
```

## 推奨コミット

```text
S78-8: Port pinned Sapphire POWCNT and framebuffer assignment verbatim
```

---

# Phase 6 — Reset・InitFramebuffers・BlankFrameをSapphire化

## 目的

cold-start時の初期状態とbuffer lifecycleを固定Sapphireへ合わせる。

## 対象

```text
src/GPU.cpp
src/GPU.h
src/GPU_Soft.cpp
```

## 作業

固定Sapphireどおり:

```text
accelerated framebuffer size:
    (256*3 + 1) * 192

non-accelerated:
    256 * 192
```

全4面を確保。

Reset時の初期値:

```text
0xFFFFFFFF
```

順序:

```text
1. GPU state reset
2. VRAM state reset
3. framebuffer初期化
4. Unit A Reset
5. Unit B Reset
6. GPU3D Reset
7. AssignFramebuffers
8. ResetVRAMCache
9. dirty flags初期化
```

`BlankFrame()`も固定Sapphireのbuffer toggle・clear・assign順序へ合わせる。

## 完了条件

```text
Reset fill値一致
framebuffer size一致
Unit reset順序一致
AssignFramebuffers timing一致
BlankFrame順序一致
```

## 推奨コミット

```text
S78-9: Restore pinned Sapphire framebuffer reset and blank-frame lifecycle
```

---

# Phase 7 — Unit B固有SEGVを修正

## 目的

stack/sanitizerで確定した直接原因を最小修正する。

## 分岐A: BBG/BOBJ coherence

faultが次の場合:

```text
VRAMDirty_BBG
VRAMFlat_BBG
VRAMDirty_BBGExtPal
VRAMFlat_BBGExtPal
VRAMDirty_BOBJExtPal
VRAMFlat_BOBJExtPal
```

固定Sapphireと次を比較。

```text
array size
mapping granularity
template Size
CopyLinearVRAM block size
VRAM mask
dirty bitfield初期化
constructor/reset order
```

独自bounds clampを足す前に、
宣言・template引数・配列寸法を固定Sapphireへ戻す。

## 分岐B: transient sprite/window state

高確率仮説:

```text
OBJLine[2][256]
OBJWindow[2][256]
NumSprites[2]
WindowMask
BGOBJLine
CurUnit
CurBGXMosaicTable
Framebuffer[2]
```

に明示初期値がない。

first lineでは:

```text
DrawScanline(0)
DrawSprites(1)
```

の順なので、sprite line 0の事前生成前に参照され得る。

sanitizerが未初期化を確認した場合、
Sapphire sourceへupstream-compatible portability patchを追加。

```cpp
SoftRenderer::SoftRenderer(GPU& gpu)
    : Renderer2D(),
      GPU(gpu),
      _3DLine(nullptr),
      CurBGXMosaicTable(MosaicTable[0].data()),
      CurUnit(nullptr)
{
    std::memset(BGOBJLine, 0, sizeof(BGOBJLine));
    std::memset(WindowMask, 0, sizeof(WindowMask));
    std::memset(OBJLine, 0, sizeof(OBJLine));
    std::memset(OBJWindow, 0, sizeof(OBJWindow));
    std::memset(NumSprites, 0, sizeof(NumSprites));
    SetFramebuffer(nullptr, nullptr);
}
```

production draw前には正規`AssignFramebuffers()`が成功していることをassertする。

## patch管理

この変更はDesktop専用workaroundにしない。

```text
upstream file
upstream commit
patch diff
patch SHA-256
理由
再現test
sanitizer result
```

をvendor manifestへ記録。

## 禁止事項

```text
- Unit Bだけ描画skip
- Unit BだけSoftware fallback
- line 0だけ白塗り
- exceptionをcatchして続行
- nullならreturnしてクラッシュだけ隠す
- 原因未確認で配列全体を巨大化
```

## 完了条件

```text
symbolized fault解消
ASan/UBSan green
Unit B line 0 complete到達
first RunFrame complete到達
```

## 推奨コミット

```text
S78-10: Fix confirmed Unit-B fault using upstream-compatible Sapphire patch
```

---

# Phase 8 — outer SoftRendererの責務縮小

## 目的

GPU2D semanticsとDesktop presentationを分離する。

## 対象

```text
src/GPU_Soft.cpp
src/GPU_Soft.h
src/SapphirePublished2DFrame.*
src/frontend/qt_sdl/*
```

## Vulkan pathから削除

```text
SoftRenderer::DrawScanline内のSapphire A/B描画
SoftRenderer::DrawSprites内のSapphire A/B描画
毎scanline framebuffer rebinding
GPU2D VBlank/VBlankEnd forwarding
特殊レジスタmirror
```

## 残してよい責務

```text
legacy non-Vulkan software renderer
3D compatibility host
completed framebuffer publication
physical top/bottom view生成
Desktop RendererOutput作成
```

## 最終構造

```text
GPU
 ├─ Sapphire Unit A
 ├─ Sapphire Unit B
 ├─ Sapphire Renderer2D
 ├─ GPU3D Renderer
 └─ GPU-owned framebuffer
          ↓
completed frame publication
          ↓
FrameLatch / FrameQueue
          ↓
VulkanOutput / Presenter
          ↓
CustomHUD final pass
```

## 完了条件

```text
GPU2D描画にouter Rendererが介在しない
Desktop差分がframe completion以降に限定される
```

## 推奨コミット

```text
S78-11: Remove Desktop outer-renderer bridge from canonical Sapphire GPU2D path
```

---

# Phase 9 — parity testを文字列検査から実装比較へ変更

## 目的

「関数名がある」だけではなく、呼出し順序と所有関係を検証する。

## 対象

```text
tools/test_sapphire_gpu2d_lifecycle_parity.py
tools/test_sapphire_gpu2d_runtime_gate_parity.py
tools/test_sapphire_vendor_parity.py
tools/sapphire_vendor_manifest.json
```

## 追加する比較

固定Sapphireとnormalized function bodyを比較。

```text
AssignFramebuffers
InitFramebuffers
SetPowerCnt
DisplayFIFO
StartFrame
StartHBlank
StartScanline
FinishFrame
BlankFrame
```

許容変換:

```text
namespace
include
logging
Desktop publication hook
build gate
```

検出対象:

```text
Rend->DrawScanlineがVulkan GPU2D pathに残る
Rend->DrawSpritesがVulkan GPU2D pathに残る
Unit順序がB→A
SetPowerCnt後にAssignFramebuffersがない
ScreenSwap shadowをbinding sourceに使用
GPU側CaptureCntが残る
GPU側FIFOが残る
GPU側MasterBrightnessがcanonical
```

## runtime differential test

同一synthetic register sequenceを:

```text
pinned Sapphire core
MelonPrime canonical core
```

へ入力し、各scanline後に比較。

```text
Unit registers
window state
mosaic counters
framebuffer hashes
capture state
FIFO pointers
```

## 完了条件

```text
static文字列存在testだけに依存しない
lifecycle呼出し順序を検証
register ownerを検証
runtime state hash一致
```

## 推奨コミット

```text
S78-12: Replace GPU2D source-presence checks with normalized lifecycle parity tests
S78-13: Add executable Sapphire GPU2D lifecycle differential test
```

---

# Phase 10 — cold-start CIを全構成でgreen化

## 目的

Debugだけ、Releaseだけ、Windowsだけで成立する修正を防ぐ。

## 必須matrix

```text
Windows MinGW Release
Windows MinGW Debug
Linux GCC Debug
Linux Clang ASan
Linux Clang UBSan
Linux Clang ASan+UBSan
可能ならLinux MSan
```

## 各jobの必須checkpoint

```text
Sapphire Vulkan activation complete
producerBegin enter
first RunFrame begin
Unit A line 0 complete
Unit B line 0 complete
first RunFrame complete
producer transaction complete
queuePush
surfacePresent=1
splashHidden=1
cold-start complete exitCode=0
```

## artifact

```text
binary
symbols
full log
config
fixture SHA
minidump/core dump
sanitizer log
commit metadata
```

## 完了条件

全jobで:

```text
exit code 0
missing checkpoint 0
sanitizer error 0
crash dump 0
```

## 推奨コミット

```text
S78-14: Require real Vulkan ROM cold-start on Windows and Linux build matrices
```

---

# Phase 11 — golden test差替え

## 目的

cold-startがgreenになる前に画像goldenへ進まない。

## 前提

Phase 0からPhase 10が完了していること。

## fixture

最初はsynthetic ROM。

次に、ライセンス上問題のないhomebrew fixtureで:

```text
2D text
Unit A BG
Unit B BG
OBJ
window
mosaic
screen swap
display capture
3D slot
brightness
```

を含むgoldenを作る。

## 比較項目

```text
packed top framebuffer
packed bottom framebuffer
structured plane 0
structured plane 1
structured control plane
screen identity
capture output
frame hash
```

## 完了条件

```text
1x
2x
4x
screen swap off/on
cold start
renderer再選択
fullscreen遷移
```

でgolden一致。

## 推奨コミット

```text
S78-15: Replace provisional Vulkan goldens after canonical GPU2D cold-start is green
```

---

# 6. 推奨コミット順

```text
S78-1  Freeze Unit-B cold-start crash reproduction artifacts
S78-2  Add first-frame Unit-B sub-stage crash trace
S78-3  Add symbolized Windows Debug cold-start crash job
S78-4  Add Linux ASan and UBSan Vulkan cold-start jobs
S78-5  Add optional MemorySanitizer GPU2D cold-start job
S78-6  Port pinned Sapphire GPU2D event lifecycle verbatim
S78-7  Move FIFO capture and brightness registers into canonical Sapphire Units
S78-8  Port pinned Sapphire POWCNT and framebuffer assignment verbatim
S78-9  Restore pinned Sapphire framebuffer reset and blank-frame lifecycle
S78-10 Fix confirmed Unit-B fault using upstream-compatible Sapphire patch
S78-11 Remove Desktop outer-renderer bridge from canonical Sapphire GPU2D path
S78-12 Replace GPU2D source-presence checks with normalized lifecycle parity tests
S78-13 Add executable Sapphire GPU2D lifecycle differential test
S78-14 Require real Vulkan ROM cold-start on Windows and Linux build matrices
S78-15 Replace provisional Vulkan goldens after canonical GPU2D cold-start is green
```

---

# 7. フェーズ実行順

```text
Phase 0
  ↓
Phase 1
  ↓
Phase 2
  ↓
Phase 3
  ↓
Phase 4
  ↓
Phase 5
  ↓
Phase 6
  ↓
cold-start再実行
  ↓
Phase 7
  ↓
Phase 8
  ↓
Phase 9
  ↓
Phase 10
  ↓
Phase 11
```

Phase 7の直接SEGV修正は、
Phase 1・2で原因が確定してから行う。

ただしPhase 3から6のSapphire lifecycle完全移植により
SEGVが消えた場合も、原因を「偶然直った」で終わらせない。

差分を二分探索し、どの構造差がfaultを生んでいたか記録する。

---

# 8. 全体の禁止事項

```text
- Unit Bだけ別rendererへfallback
- Unit Bのline 0だけ描画skip
- first frameだけ白画面を許容
- ACCESS_VIOLATIONをSEHで握り潰す
- crash時にexit code 0を返す
- nullptr checkだけで処理を続行
- ScreenSwapとPowerControl9を両方canonical sourceにする
- FIFO/capture/brightnessの再同期adapterを追加
- outer Renderer経由をSapphire parityと呼ぶ
- static文字列testだけで完了扱い
- stackなしで原因を断定
- Sapphireコードを見ながら同等コードを再実装
- Sapphireから直接持ってこられる関数を独自設計へ置換
- Androidの偶然のzero allocationへ依存
- cold-start green前にgoldenを確定
```

---

# 9. 最終完了条件

## 実行

```text
ACCESS_VIOLATION = 0
first RunFrame complete
producer complete
queuePush
surfacePresent = 1
splashHidden = 1
cold-start exit code = 0
```

## 所有

```text
canonical Unit A/B = 1組
GPU2D Renderer2D owner = GPU
framebuffer owner = GPU
CaptureCnt owner = Unit A
FIFO owner = Unit A
MasterBrightness owner =各Unit
screen swap source = PowerControl9 bit15
```

## lifecycle

```text
StartHBlank順序 = 固定Sapphire
StartScanline順序 = 固定Sapphire
VBlank順序 = 固定Sapphire
VBlankEnd順序 = 固定Sapphire
DrawScanline A→B
DrawSprites A→B
FinishFrame toggle/assign一致
SetPowerCnt直後assign
```

## platform差

許可:

```text
Qt WSI
VkSurfaceKHR
Vulkan loader
queue family
swapchain
timeline/fence lifetime
window events
CustomHUD
logging
minidump
```

GPU2D semantics差:

```text
0
```

## CI

```text
Windows Release green
Windows Debug green
Linux Debug green
ASan green
UBSan green
未初期化検査green
normalized parity green
runtime differential green
cold-start green
golden green
```

---

# 10. 最終判断

S77-9からS77-11で、

```text
Unit A/B
GPU2D Renderer2D
framebuffer
```

の所有方向はSapphireへ近づいた。

しかし現状は、

```text
Sapphire data model
+
旧Desktop event engine
+
特殊register二重所有
```

である。

Unit B first RunFrame SEGVは、
この混成境界で発生している可能性が高い。

---

# 11. 進捗記録

| Phase | Commit | Status | Summary |
|-------|--------|--------|---------|
| S78-1 | `9463e8d3a` | done | Cold-start crash reproduction artifacts + frozen baseline test |
| S78-2 | `2966dfaed` | done | First-frame Unit-B sub-stage crash trace |
| S78-3/4/5 | `7a02bc409` | done | Debug + ASan/UBSan/MSan CMake flags and CI jobs |
| S78-6 | `df4dd8894` | done | Sapphire GPU2D event lifecycle ported to GPU2D_Renderer |
| S78-7/8/9/10/11 | `d00cba24a` | done | Canonical Units, POWCNT binding, reset fill, bridge removal |
| S78-12/13/14 | `c2526efc3` | done | Lifecycle parity/differential tests + CI matrix updates |
| S78-15 | (pending) | blocked | Golden fixture replacement waits on cold-start green |

安全な修正方針はUnit Bへ例外を足すことではない。

```text
固定SapphireのGPU2D event lifecycleと
特殊register ownershipを
dependency closureごと移植する
```

こと。

その後も残るWindows固有faultが未初期化transient stateであれば、
Sapphire sourceへupstream-compatibleな決定論的初期化patchを追加する。

最終的なAndroid／Desktop差は、

```text
表示先
resource lifetime
window lifecycle
HUD
```

だけに限定する。
